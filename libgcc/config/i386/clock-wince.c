/* Clocks for the Windows CE thread and C++ runtime.  */

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


/* Windows CE 5.0 has no GetSystemTimeAsFileTime, and on many devices
   GetSystemTime only counts whole seconds (wMilliseconds is 0), so a
   short timed wait measured against it never sees time pass.  These
   clocks count milliseconds of GetTickCount instead:

   __wince_clock_monotonic is GetTickCount widened to 64 bits, so that
   it does not wrap after 49.7 days.  It backs std::chrono::steady_clock.

   __wince_clock_realtime is the system time advanced by GetTickCount
   since it was last read.  It is moved to the system time whenever it
   falls behind it or runs a second or more ahead of it (the system
   time was set, or its second ticked), so it is the system time with
   millisecond steps.  It backs std::chrono::system_clock and the
   absolute timeouts of the thread layer (gthr-wince.c), which must use
   the same clock.  */

#include <time.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* The number of 100-nanoseconds between 1/1/1601 and 1/1/1970.  */
#define FILETIME_1970 116444736000000000ULL
#define NSEC100_PER_SEC 10000000ULL
#define NSEC100_PER_MSEC 10000ULL

void __wince_clock_monotonic (struct timespec *);
void __wince_clock_realtime (struct timespec *);

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

/* Last GetTickCount and the number of times it wrapped; under LOCK.  */
static DWORD last_tick;
static DWORD tick_wraps;

static ULONGLONG
monotonic_ms_locked (void)
{
  DWORD tick = GetTickCount ();
  if (tick < last_tick)
    tick_wraps++;
  last_tick = tick;
  return ((ULONGLONG) tick_wraps << 32) | tick;
}

static void
to_timespec (ULONGLONG nsec100, struct timespec *ts)
{
  ts->tv_sec = (time_t) (nsec100 / NSEC100_PER_SEC);
  ts->tv_nsec = (long) (nsec100 % NSEC100_PER_SEC) * 100;
}

void
__wince_clock_monotonic (struct timespec *ts)
{
  ULONGLONG ms;

  lock ();
  ms = monotonic_ms_locked ();
  unlock ();
  to_timespec (ms * NSEC100_PER_MSEC, ts);
}

/* The system time (since 1970, in 100 ns) and the tick it was read at.  */
static int have_base;
static ULONGLONG base_nsec100;
static ULONGLONG base_ms;

void
__wince_clock_realtime (struct timespec *ts)
{
  SYSTEMTIME st;
  union {
    ULONGLONG nsec100;
    FILETIME ft;
  } sys;
  ULONGLONG ms, now;

  GetSystemTime (&st);
  if (!SystemTimeToFileTime (&st, &sys.ft))
    sys.nsec100 = FILETIME_1970;
  sys.nsec100 -= FILETIME_1970;

  lock ();
  ms = monotonic_ms_locked ();
  now = base_nsec100 + (ms - base_ms) * NSEC100_PER_MSEC;
  if (!have_base || now < sys.nsec100
      || now >= sys.nsec100 + NSEC100_PER_SEC)
    {
      have_base = 1;
      base_nsec100 = now = sys.nsec100;
      base_ms = ms;
    }
  unlock ();
  to_timespec (now, ts);
}
