/* __atomic library calls for Windows CE.  */

/* Copyright (C) 2026 Free Software Foundation, Inc.

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

/* arm-mingw32ce (ARMv4) and sh4-mingw32ce have no atomic instructions
   that GCC expands inline, and there is no libatomic for them, so every
   std::atomic read-modify-write becomes a call to __atomic_*_N.  These
   are those calls: the 1-, 2- and 4-byte ones on coredll's
   Interlocked functions (1 and 2 bytes by compare-and-swap of the word
   that holds them, so that GCC's inline loads and stores of them stay
   atomic with these), the others under one lock.  The lock is a spin lock whose waiters
   sleep, so a waiting thread lets a preempted lower-priority holder
   run.  */

#include <stddef.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define STR1(X) #X
#define STR(X) STR1 (X)
/* The library names of the builtins; they cannot be defined under their
   own names in C.  */
#define ASMNAME(N) __asm__ (STR (__USER_LABEL_PREFIX__) #N)

typedef unsigned char U1;
typedef unsigned short U2;
typedef unsigned int U4;
typedef unsigned long long U8;

static LONG volatile lock_word;

static void
lock (void)
{
  int n = 0;
  while (InterlockedExchange (&lock_word, 1))
    Sleep (++n < 4 ? 0 : 1);
}

static void
unlock (void)
{
  InterlockedExchange (&lock_word, 0);
}

/* ------------------------------------------------- 4 bytes, Interlocked  */

U4 atomic_load_4 (const volatile void *p, int m) ASMNAME (__atomic_load_4);
U4
atomic_load_4 (const volatile void *p, int m)
{
  (void) m;
  return (U4) InterlockedCompareExchange ((LONG volatile *) p, 0, 0);
}

void atomic_store_4 (volatile void *p, U4 v, int m) ASMNAME (__atomic_store_4);
void
atomic_store_4 (volatile void *p, U4 v, int m)
{
  (void) m;
  InterlockedExchange ((LONG volatile *) p, (LONG) v);
}

U4 atomic_exchange_4 (volatile void *p, U4 v, int m)
  ASMNAME (__atomic_exchange_4);
U4
atomic_exchange_4 (volatile void *p, U4 v, int m)
{
  (void) m;
  return (U4) InterlockedExchange ((LONG volatile *) p, (LONG) v);
}

_Bool atomic_compare_exchange_4 (volatile void *p, void *e, U4 d, int s, int f)
  ASMNAME (__atomic_compare_exchange_4);
_Bool
atomic_compare_exchange_4 (volatile void *p, void *e, U4 d, int s, int f)
{
  U4 want = *(U4 *) e;
  U4 old = (U4) InterlockedCompareExchange ((LONG volatile *) p, (LONG) d,
					     (LONG) want);
  (void) s;
  (void) f;
  if (old == want)
    return 1;
  *(U4 *) e = old;
  return 0;
}

/* Read-modify-write by compare-and-swap.  */
#define RMW4(NAME, EXPR)						\
  static U4								\
  rmw4_##NAME (volatile void *p, U4 v, U4 *newp)			\
  {									\
    U4 old, new_;							\
    do									\
      {									\
	old = *(volatile U4 *) p;					\
	new_ = (EXPR);							\
      }									\
    while ((U4) InterlockedCompareExchange ((LONG volatile *) p,	\
					    (LONG) new_, (LONG) old)	\
	   != old);							\
    *newp = new_;							\
    return old;								\
  }									\
  U4 atomic_fetch_##NAME##_4 (volatile void *, U4, int)			\
    ASMNAME (__atomic_fetch_##NAME##_4);				\
  U4									\
  atomic_fetch_##NAME##_4 (volatile void *p, U4 v, int m)		\
  {									\
    U4 n;								\
    (void) m;								\
    return rmw4_##NAME (p, v, &n);					\
  }									\
  U4 atomic_##NAME##_fetch_4 (volatile void *, U4, int)			\
    ASMNAME (__atomic_##NAME##_fetch_4);				\
  U4									\
  atomic_##NAME##_fetch_4 (volatile void *p, U4 v, int m)		\
  {									\
    U4 n;								\
    (void) m;								\
    rmw4_##NAME (p, v, &n);						\
    return n;								\
  }

RMW4 (add, old + v)
RMW4 (sub, old - v)
RMW4 (and, old & v)
RMW4 (or, old | v)
RMW4 (xor, old ^ v)
RMW4 (nand, ~(old & v))

/* ----------------------------------- 1 and 2 bytes, CAS of their word  */

/* Replace the T at P by F (old) and return the old value; F runs again
   when another thread changed the word in between.  */
#define SUBWORD(N, T)							\
  static T								\
  rmw_##N (volatile void *p, T (*f) (T, T), T v, T *newp)		\
  {									\
    volatile U4 *w = (volatile U4 *) ((size_t) p & ~(size_t) 3);	\
    unsigned shift = ((size_t) p & 3) * 8;				\
    U4 mask = (U4) (T) ~(T) 0 << shift;					\
    U4 ow, nw;								\
    T old, new_;							\
    do									\
      {									\
	ow = *w;							\
	old = (T) (ow >> shift);					\
	new_ = f (old, v);						\
	nw = (ow & ~mask) | ((U4) new_ << shift);			\
      }									\
    while ((U4) InterlockedCompareExchange ((LONG volatile *) w,	\
					    (LONG) nw, (LONG) ow) != ow); \
    if (newp)								\
      *newp = new_;							\
    return old;								\
  }									\
  static T op_xchg_##N (T o, T v) { (void) o; return v; }		\
  static T op_add_##N (T o, T v) { return o + v; }			\
  static T op_sub_##N (T o, T v) { return o - v; }			\
  static T op_and_##N (T o, T v) { return o & v; }			\
  static T op_or_##N (T o, T v) { return o | v; }			\
  static T op_xor_##N (T o, T v) { return o ^ v; }			\
  static T op_nand_##N (T o, T v) { return ~(o & v); }			\
  T atomic_load_##N (const volatile void *, int)			\
    ASMNAME (__atomic_load_##N);					\
  T									\
  atomic_load_##N (const volatile void *p, int m)			\
  {									\
    (void) m;								\
    return *(const volatile T *) p;					\
  }									\
  void atomic_store_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_store_##N);					\
  void									\
  atomic_store_##N (volatile void *p, T v, int m)			\
  {									\
    (void) m;								\
    rmw_##N (p, op_xchg_##N, v, NULL);					\
  }									\
  T atomic_exchange_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_exchange_##N);					\
  T									\
  atomic_exchange_##N (volatile void *p, T v, int m)			\
  {									\
    (void) m;								\
    return rmw_##N (p, op_xchg_##N, v, NULL);				\
  }									\
  _Bool atomic_compare_exchange_##N (volatile void *, void *, T, int, int) \
    ASMNAME (__atomic_compare_exchange_##N);				\
  _Bool									\
  atomic_compare_exchange_##N (volatile void *p, void *e, T d, int s,	\
			       int f)					\
  {									\
    volatile U4 *w = (volatile U4 *) ((size_t) p & ~(size_t) 3);	\
    unsigned shift = ((size_t) p & 3) * 8;				\
    U4 mask = (U4) (T) ~(T) 0 << shift;					\
    U4 ow, nw;								\
    T want = *(T *) e;							\
    (void) s;								\
    (void) f;								\
    for (;;)								\
      {									\
	ow = *w;							\
	if ((T) (ow >> shift) != want)					\
	  {								\
	    *(T *) e = (T) (ow >> shift);				\
	    return 0;							\
	  }								\
	nw = (ow & ~mask) | ((U4) d << shift);				\
	if ((U4) InterlockedCompareExchange ((LONG volatile *) w,	\
					     (LONG) nw, (LONG) ow) == ow) \
	  return 1;							\
      }									\
  }									\
  SUBWORD_RMW (N, T, add)						\
  SUBWORD_RMW (N, T, sub)						\
  SUBWORD_RMW (N, T, and)						\
  SUBWORD_RMW (N, T, or)						\
  SUBWORD_RMW (N, T, xor)						\
  SUBWORD_RMW (N, T, nand)

#define SUBWORD_RMW(N, T, NAME)						\
  T atomic_fetch_##NAME##_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_fetch_##NAME##_##N);				\
  T									\
  atomic_fetch_##NAME##_##N (volatile void *p, T v, int m)		\
  {									\
    (void) m;								\
    return rmw_##N (p, op_##NAME##_##N, v, NULL);			\
  }									\
  T atomic_##NAME##_fetch_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_##NAME##_fetch_##N);				\
  T									\
  atomic_##NAME##_fetch_##N (volatile void *p, T v, int m)		\
  {									\
    T n;								\
    (void) m;								\
    rmw_##N (p, op_##NAME##_##N, v, &n);				\
    return n;								\
  }

SUBWORD (1, U1)
SUBWORD (2, U2)

/* --------------------------------------------- ----- 8 bytes, lock  */

#define LOCKED(N, T)							\
  T atomic_load_##N (const volatile void *, int)			\
    ASMNAME (__atomic_load_##N);					\
  T									\
  atomic_load_##N (const volatile void *p, int m)			\
  {									\
    T r;								\
    (void) m;								\
    lock ();								\
    r = *(const volatile T *) p;					\
    unlock ();								\
    return r;								\
  }									\
  void atomic_store_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_store_##N);					\
  void									\
  atomic_store_##N (volatile void *p, T v, int m)			\
  {									\
    (void) m;								\
    lock ();								\
    *(volatile T *) p = v;						\
    unlock ();								\
  }									\
  T atomic_exchange_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_exchange_##N);					\
  T									\
  atomic_exchange_##N (volatile void *p, T v, int m)			\
  {									\
    T r;								\
    (void) m;								\
    lock ();								\
    r = *(volatile T *) p;						\
    *(volatile T *) p = v;						\
    unlock ();								\
    return r;								\
  }									\
  _Bool atomic_compare_exchange_##N (volatile void *, void *, T, int, int) \
    ASMNAME (__atomic_compare_exchange_##N);				\
  _Bool									\
  atomic_compare_exchange_##N (volatile void *p, void *e, T d, int s,	\
			       int f)					\
  {									\
    _Bool ok;								\
    T cur;								\
    (void) s;								\
    (void) f;								\
    lock ();								\
    cur = *(volatile T *) p;						\
    ok = cur == *(T *) e;						\
    if (ok)								\
      *(volatile T *) p = d;						\
    else								\
      *(T *) e = cur;							\
    unlock ();								\
    return ok;								\
  }									\
  LOCKED_RMW (N, T, add, old + v)					\
  LOCKED_RMW (N, T, sub, old - v)					\
  LOCKED_RMW (N, T, and, old & v)					\
  LOCKED_RMW (N, T, or, old | v)					\
  LOCKED_RMW (N, T, xor, old ^ v)					\
  LOCKED_RMW (N, T, nand, ~(old & v))

#define LOCKED_RMW(N, T, NAME, EXPR)					\
  T atomic_fetch_##NAME##_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_fetch_##NAME##_##N);				\
  T									\
  atomic_fetch_##NAME##_##N (volatile void *p, T v, int m)		\
  {									\
    T old;								\
    (void) m;								\
    lock ();								\
    old = *(volatile T *) p;						\
    *(volatile T *) p = (T) (EXPR);					\
    unlock ();								\
    return old;								\
  }									\
  T atomic_##NAME##_fetch_##N (volatile void *, T, int)			\
    ASMNAME (__atomic_##NAME##_fetch_##N);				\
  T									\
  atomic_##NAME##_fetch_##N (volatile void *p, T v, int m)		\
  {									\
    T old, new_;							\
    (void) m;								\
    lock ();								\
    old = *(volatile T *) p;						\
    new_ = (T) (EXPR);							\
    *(volatile T *) p = new_;						\
    unlock ();								\
    return new_;							\
  }

LOCKED (8, U8)

/* ------------------------------------------------------ any size, lock  */

void atomic_load (size_t, const volatile void *, void *, int)
  ASMNAME (__atomic_load);
void
atomic_load (size_t n, const volatile void *p, void *r, int m)
{
  (void) m;
  lock ();
  memcpy (r, (const void *) p, n);
  unlock ();
}

void atomic_store (size_t, volatile void *, void *, int)
  ASMNAME (__atomic_store);
void
atomic_store (size_t n, volatile void *p, void *v, int m)
{
  (void) m;
  lock ();
  memcpy ((void *) p, v, n);
  unlock ();
}

void atomic_exchange (size_t, volatile void *, void *, void *, int)
  ASMNAME (__atomic_exchange);
void
atomic_exchange (size_t n, volatile void *p, void *v, void *r, int m)
{
  (void) m;
  lock ();
  memcpy (r, (const void *) p, n);
  memcpy ((void *) p, v, n);
  unlock ();
}

_Bool atomic_compare_exchange (size_t, volatile void *, void *, void *, int,
			       int) ASMNAME (__atomic_compare_exchange);
_Bool
atomic_compare_exchange (size_t n, volatile void *p, void *e, void *d, int s,
			 int f)
{
  _Bool ok;
  (void) s;
  (void) f;
  lock ();
  ok = memcmp ((const void *) p, e, n) == 0;
  if (ok)
    memcpy ((void *) p, d, n);
  else
    memcpy (e, (const void *) p, n);
  unlock ();
  return ok;
}

/* The 1-, 2- and 4-byte operations do not take the lock.  */

_Bool atomic_is_lock_free (size_t, const volatile void *)
  ASMNAME (__atomic_is_lock_free);
_Bool
atomic_is_lock_free (size_t n, const volatile void *p)
{
  return (n == 1 || n == 2 || n == 4) && ((size_t) p & (n - 1)) == 0;
}
