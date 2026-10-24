/* Threads compatibility routines for libgcc2 on Windows CE.  */

/* Copyright (C) 1999-2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* The implementation of the UNDER_CE part of gthr-win32.h, which
   describes the design.  It uses only coredll functions of the Windows
   CE 5.0 SDK: no SRW locks, condition variables, GetThreadId or
   GetSystemTimeAsFileTime.  This module is in libgcc.a only once, so
   the TLS slot of the thread descriptors and the list of key destructors
   exist once per program.  */

#include "gthr-win32.h"

#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef ERROR_NOT_ENOUGH_MEMORY
#define ERROR_NOT_ENOUGH_MEMORY 8
#endif

/* Atomic accesses go through coredll's Interlocked functions: ARMv4 and
   SH-4 have no instructions for them that GCC could expand inline.  */
#define ATOMIC_XCHG(P, V) InterlockedExchange ((LONG volatile *) (P), (LONG) (V))
#define ATOMIC_CAS(P, NEW, OLD) \
  InterlockedCompareExchange ((LONG volatile *) (P), (LONG) (NEW), (LONG) (OLD))
#define ATOMIC_LOAD(P) ATOMIC_CAS ((P), 0, 0)

/* Return a nonzero error code, GetLastError's if it has one.  */

static int
last_error (void)
{
  DWORD e = GetLastError ();
  return e ? (int) e : 1;
}

/* ------------------------------------------------------------- once  */

int
__gthr_wince_once (__gthread_once_t *once, void (*func) (void))
{
  if (once == NULL || func == NULL)
    return 1;

  if (!ATOMIC_LOAD (&once->done))
    {
      if (InterlockedIncrement ((LONG volatile *) &once->started) == 0)
	{
	  (*func) ();
	  ATOMIC_XCHG (&once->done, 1);
	}
      else
	/* Another thread runs FUNC.  Sleep (0) only yields to threads of
	   the same priority, Sleep (1) to all.  */
	while (!ATOMIC_LOAD (&once->done))
	  Sleep (1);
    }
  return 0;
}

/* ---------------------------------------------------------- mutexes  */

/* The semaphore of a mutex, created on the first contention.  Both the
   locking and the unlocking thread may create it; one of them wins.  */

static HANDLE
mutex_sema (void *volatile *slot)
{
  HANDLE s = (HANDLE) *slot;
  if (s)
    return s;
  s = CreateSemaphoreW (NULL, 0, 0x7fffffff, NULL);
  if (!s)
    abort ();
  HANDLE old = (HANDLE) ATOMIC_CAS (slot, s, NULL);
  if (old)
    {
      CloseHandle (s);
      return old;
    }
  return s;
}

void
__gthr_wince_mutex_init_function (__gthread_mutex_t *mutex)
{
  mutex->counter = -1;
  mutex->sema = NULL;
}

void
__gthr_wince_mutex_destroy (__gthread_mutex_t *mutex)
{
  if (mutex->sema)
    CloseHandle ((HANDLE) mutex->sema);
  mutex->sema = NULL;
}

int
__gthr_wince_mutex_lock (__gthread_mutex_t *mutex)
{
  if (InterlockedIncrement (&mutex->counter) == 0)
    return 0;
  if (WaitForSingleObject (mutex_sema (&mutex->sema), INFINITE)
      == WAIT_OBJECT_0)
    return 0;
  /* WAIT_FAILED: best-effort cleanup only.  */
  InterlockedDecrement (&mutex->counter);
  return 1;
}

int
__gthr_wince_mutex_trylock (__gthread_mutex_t *mutex)
{
  return ATOMIC_CAS (&mutex->counter, 0, -1) == -1 ? 0 : 1;
}

int
__gthr_wince_mutex_unlock (__gthread_mutex_t *mutex)
{
  if (InterlockedDecrement (&mutex->counter) >= 0)
    return ReleaseSemaphore (mutex_sema (&mutex->sema), 1, NULL) ? 0 : 1;
  return 0;
}

void
__gthr_wince_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  mutex->counter = -1;
  mutex->depth = 0;
  mutex->owner = 0;
  mutex->sema = NULL;
}

void
__gthr_wince_recursive_mutex_destroy (__gthread_recursive_mutex_t *mutex)
{
  if (mutex->sema)
    CloseHandle ((HANDLE) mutex->sema);
  mutex->sema = NULL;
}

int
__gthr_wince_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  DWORD me = GetCurrentThreadId ();
  if (InterlockedIncrement (&mutex->counter) == 0)
    {
      mutex->depth = 1;
      mutex->owner = me;
    }
  else if (mutex->owner == me)
    {
      InterlockedDecrement (&mutex->counter);
      ++mutex->depth;
    }
  else if (WaitForSingleObject (mutex_sema (&mutex->sema), INFINITE)
	   == WAIT_OBJECT_0)
    {
      mutex->depth = 1;
      mutex->owner = me;
    }
  else
    {
      InterlockedDecrement (&mutex->counter);
      return 1;
    }
  return 0;
}

int
__gthr_wince_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
{
  DWORD me = GetCurrentThreadId ();
  if (ATOMIC_CAS (&mutex->counter, 0, -1) == -1)
    {
      mutex->depth = 1;
      mutex->owner = me;
    }
  else if (mutex->owner == me)
    ++mutex->depth;
  else
    return 1;
  return 0;
}

int
__gthr_wince_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  if (--mutex->depth == 0)
    {
      mutex->owner = 0;
      if (InterlockedDecrement (&mutex->counter) >= 0)
	return ReleaseSemaphore (mutex_sema (&mutex->sema), 1, NULL) ? 0 : 1;
    }
  return 0;
}

/* -------------------------------------------------------- TLS keys  */

/* The keys that have a destructor.  Windows CE has at most 64 TLS
   slots per process.  */
#define KEYS_MAX 64

static struct
{
  DWORD key;
  void (*dtor) (void *);
} keys[KEYS_MAX];
static int nkeys;
static __gthread_mutex_t keys_lock = {-1, NULL};

int
__gthr_wince_key_create (__gthread_key_t *key, void (*dtor) (void *))
{
  DWORD k = TlsAlloc ();
  if (k == TLS_OUT_OF_INDEXES)
    return last_error ();
  *key = k;
  if (dtor)
    {
      __gthr_wince_mutex_lock (&keys_lock);
      if (nkeys < KEYS_MAX)
	{
	  keys[nkeys].key = k;
	  keys[nkeys].dtor = dtor;
	  ++nkeys;
	}
      __gthr_wince_mutex_unlock (&keys_lock);
    }
  return 0;
}

int
__gthr_wince_key_delete (__gthread_key_t key)
{
  int i;
  __gthr_wince_mutex_lock (&keys_lock);
  for (i = 0; i < nkeys; ++i)
    if (keys[i].key == key)
      {
	keys[i] = keys[--nkeys];
	break;
      }
  __gthr_wince_mutex_unlock (&keys_lock);
  return TlsFree (key) ? 0 : last_error ();
}

void *
__gthr_wince_getspecific (__gthread_key_t key)
{
  DWORD e = GetLastError ();
  void *p = TlsGetValue (key);
  SetLastError (e);
  return p;
}

int
__gthr_wince_setspecific (__gthread_key_t key, const void *ptr)
{
  return TlsSetValue (key, (void *) ptr) ? 0 : last_error ();
}

/* Run the destructors of the keys for the current thread, as POSIX
   does: in rounds, while a destructor leaves a value behind.  */

static void
run_key_dtors (void)
{
  int round, i, n, again;
  DWORD k[KEYS_MAX];
  void (*d[KEYS_MAX]) (void *);

  for (round = 0; round < 4; ++round)
    {
      __gthr_wince_mutex_lock (&keys_lock);
      n = nkeys;
      for (i = 0; i < n; ++i)
	{
	  k[i] = keys[i].key;
	  d[i] = keys[i].dtor;
	}
      __gthr_wince_mutex_unlock (&keys_lock);

      again = 0;
      for (i = 0; i < n; ++i)
	{
	  void *p = TlsGetValue (k[i]);
	  if (p)
	    {
	      TlsSetValue (k[i], NULL);
	      d[i] (p);
	      again = 1;
	    }
	}
      if (!again)
	break;
    }
}

/* ---------------------------------------------------------- threads  */

enum { THR_RUNNING, THR_DONE, THR_DETACHED, THR_ADOPTED };

struct __gthr_wince_thread
{
  void *(*func) (void *);
  void *args;
  /* NULL for a thread that libgcc did not create.  */
  HANDLE h;
  DWORD id;
  LONG state;
};

/* The TLS slot that holds each thread's descriptor.  */
static DWORD self_key = TLS_OUT_OF_INDEXES;
static __gthread_once_t self_once = __GTHREAD_ONCE_INIT;

static void
self_init (void)
{
  self_key = TlsAlloc ();
  if (self_key == TLS_OUT_OF_INDEXES)
    abort ();
}

static DWORD WINAPI
thread_start (LPVOID arg)
{
  struct __gthr_wince_thread *td = (struct __gthr_wince_thread *) arg;
  void *ret;

  TlsSetValue (self_key, td);
  ret = td->func (td->args);
  run_key_dtors ();
  TlsSetValue (self_key, NULL);
  /* After a detach nobody else holds the descriptor.  */
  if (ATOMIC_XCHG (&td->state, THR_DONE) == THR_DETACHED)
    free (td);
  return (DWORD) ret;
}

int
__gthr_wince_create (__gthread_t *thr, void *(*func) (void *), void *args)
{
  struct __gthr_wince_thread *td;

  __gthr_wince_once (&self_once, self_init);
  td = (struct __gthr_wince_thread *) malloc (sizeof *td);
  if (!td)
    return ERROR_NOT_ENOUGH_MEMORY;
  td->func = func;
  td->args = args;
  td->state = THR_RUNNING;
  td->h = CreateThread (NULL, 0, thread_start, td, CREATE_SUSPENDED, &td->id);
  if (!td->h)
    {
      int e = last_error ();
      free (td);
      return e;
    }
  *thr = td;
  ResumeThread (td->h);
  return 0;
}

int
__gthr_wince_join (__gthread_t td, void **value_ptr)
{
  if (!td || !td->h || td->id == GetCurrentThreadId ())
    return 1;
  if (WaitForSingleObject (td->h, INFINITE) != WAIT_OBJECT_0)
    return last_error ();
  if (value_ptr)
    {
      DWORD code = 0;
      GetExitCodeThread (td->h, &code);
      *value_ptr = (void *) code;
    }
  CloseHandle (td->h);
  free (td);
  return 0;
}

int
__gthr_wince_detach (__gthread_t td)
{
  if (!td || !td->h)
    return 1;
  CloseHandle (td->h);
  td->h = NULL;
  if (ATOMIC_XCHG (&td->state, THR_DETACHED) == THR_DONE)
    free (td);
  return 0;
}

__gthread_t
__gthr_wince_self (void)
{
  struct __gthr_wince_thread *td;
  DWORD e = GetLastError ();

  __gthr_wince_once (&self_once, self_init);
  td = (struct __gthr_wince_thread *) TlsGetValue (self_key);
  if (!td)
    {
      /* The main thread or a CreateThread thread.  Its descriptor lives
	 as long as the program.  */
      td = (struct __gthr_wince_thread *) malloc (sizeof *td);
      if (!td)
	abort ();
      td->func = NULL;
      td->args = NULL;
      td->h = NULL;
      td->id = GetCurrentThreadId ();
      td->state = THR_ADOPTED;
      TlsSetValue (self_key, td);
    }
  SetLastError (e);
  return td;
}

int
__gthr_wince_yield (void)
{
  Sleep (0);
  return 0;
}

/* ------------------------------------------------ condition variables  */

/* A waiting thread.  It lives on the waiter's stack; it is in the list
   of the condition variable from before the waiter unlocks its mutex
   until a signal or the waiter itself takes it out.  */
struct __gthr_wince_waiter
{
  struct __gthr_wince_waiter *next;
  HANDLE event;
  int woken;
};

void
__gthr_wince_cond_init_function (__gthread_cond_t *cond)
{
  __gthr_wince_mutex_init_function (&cond->lock);
  cond->head = cond->tail = NULL;
}

void
__gthr_wince_cond_destroy (__gthread_cond_t *cond)
{
  __gthr_wince_mutex_destroy (&cond->lock);
}

/* Wake the first waiter.  COND->lock is held.  */

static int
wake_one (__gthread_cond_t *cond)
{
  struct __gthr_wince_waiter *w = cond->head;
  if (!w)
    return 0;
  cond->head = w->next;
  if (!cond->head)
    cond->tail = NULL;
  w->woken = 1;
  SetEvent (w->event);
  return 1;
}

int
__gthr_wince_cond_signal (__gthread_cond_t *cond)
{
  __gthr_wince_mutex_lock (&cond->lock);
  wake_one (cond);
  __gthr_wince_mutex_unlock (&cond->lock);
  return 0;
}

int
__gthr_wince_cond_broadcast (__gthread_cond_t *cond)
{
  __gthr_wince_mutex_lock (&cond->lock);
  while (wake_one (cond))
    ;
  __gthr_wince_mutex_unlock (&cond->lock);
  return 0;
}

/* Wait on COND for at most MS milliseconds, with the caller's mutex
   released by UNLOCK and taken again by LOCK.  Returns 0 when woken by a
   signal or broadcast, ERROR_TIMEOUT otherwise.  */

static int
cond_wait (__gthread_cond_t *cond, DWORD ms,
	   void (*unlock) (void *), void (*lock) (void *), void *mutex)
{
  struct __gthr_wince_waiter w;
  int status = 0;

  w.next = NULL;
  w.woken = 0;
  w.event = CreateEventW (NULL, FALSE, FALSE, NULL);
  if (!w.event)
    return last_error ();

  __gthr_wince_mutex_lock (&cond->lock);
  if (cond->tail)
    cond->tail->next = &w;
  else
    cond->head = &w;
  cond->tail = &w;
  __gthr_wince_mutex_unlock (&cond->lock);

  unlock (mutex);
  WaitForSingleObject (w.event, ms);

  __gthr_wince_mutex_lock (&cond->lock);
  if (!w.woken)
    {
      /* Timed out (or the wait failed): take W out of the list.  */
      struct __gthr_wince_waiter **p = &cond->head, *prev = NULL;
      while (*p && *p != &w)
	{
	  prev = *p;
	  p = &(*p)->next;
	}
      if (*p)
	{
	  *p = w.next;
	  if (cond->tail == &w)
	    cond->tail = prev;
	}
      status = ERROR_TIMEOUT;
    }
  __gthr_wince_mutex_unlock (&cond->lock);

  CloseHandle (w.event);
  lock (mutex);
  return status;
}

static void
unlock_mutex (void *m)
{
  __gthr_wince_mutex_unlock ((__gthread_mutex_t *) m);
}

static void
lock_mutex (void *m)
{
  __gthr_wince_mutex_lock ((__gthread_mutex_t *) m);
}

int
__gthr_wince_cond_wait (__gthread_cond_t *cond, __gthread_mutex_t *mutex)
{
  return cond_wait (cond, INFINITE, unlock_mutex, lock_mutex, mutex);
}

/* The number of 100-nanoseconds between 1/1/1601 and 1/1/1970. */
#define FILETIME_1970 116444736000000000ULL
#define NSEC100_PER_SEC (1000000000ULL / 100)
#define NSEC100_PER_MSEC (NSEC100_PER_SEC / 1000)
#define CEIL_DIV(X, Y) (((X) + (Y) - 1) / (Y))

/* ABS_TIME (CLOCK_REALTIME) as milliseconds from now.  Windows CE 5.0
   has no GetSystemTimeAsFileTime.  */

static DWORD
abs_to_rel_ms (const __gthread_time_t *abs_time)
{
  SYSTEMTIME st;
  union {
    ULONGLONG nsec100;
    FILETIME ft;
  } now;
  ULONGLONG abs_nsec100, ms;

  GetSystemTime (&st);
  if (!SystemTimeToFileTime (&st, &now.ft))
    return 0;
  now.nsec100 -= FILETIME_1970;

  abs_nsec100 = (ULONGLONG) abs_time->tv_sec * NSEC100_PER_SEC
		+ CEIL_DIV (abs_time->tv_nsec, 100);
  if (abs_nsec100 <= now.nsec100)
    return 0;
  ms = CEIL_DIV (abs_nsec100 - now.nsec100, NSEC100_PER_MSEC);
  return ms >= INFINITE ? INFINITE - 1 : (DWORD) ms;
}

int
__gthr_wince_cond_timedwait (__gthread_cond_t *cond, __gthread_mutex_t *mutex,
			     const __gthread_time_t *abs_time)
{
  return cond_wait (cond, abs_to_rel_ms (abs_time), unlock_mutex, lock_mutex,
		    mutex);
}

/* A recursive mutex is released and taken again at its full depth.  */

struct rec_wait
{
  __gthread_recursive_mutex_t *mutex;
  int depth;
};

static void
unlock_rec (void *p)
{
  struct rec_wait *r = (struct rec_wait *) p;
  r->depth = r->mutex->depth;
  r->mutex->depth = 1;
  __gthr_wince_recursive_mutex_unlock (r->mutex);
}

static void
lock_rec (void *p)
{
  struct rec_wait *r = (struct rec_wait *) p;
  __gthr_wince_recursive_mutex_lock (r->mutex);
  r->mutex->depth = r->depth;
}

int
__gthr_wince_cond_wait_recursive (__gthread_cond_t *cond,
				  __gthread_recursive_mutex_t *mutex)
{
  struct rec_wait r = { mutex, 0 };
  return cond_wait (cond, INFINITE, unlock_rec, lock_rec, &r);
}
