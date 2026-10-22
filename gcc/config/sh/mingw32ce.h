/* Operating system specific defines for Windows CE on SH-4
   (sh4-mingw32ce), using GNU tools, mingwrt and w32api.
   Copyright (C) 2006-2020 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* Use the calling convention of the Microsoft SH-4 compiler; see
   sh.c and docs/sh4/msvc-sh4-abi-reference.txt in cegcc-build.  */
#undef TARGET_MSABI
#define TARGET_MSABI 1

/* -mms-bitfields, on by default as on arm-mingw32ce.  */
#undef SH_MS_BITFIELD_LAYOUT
#define SH_MS_BITFIELD_LAYOUT TARGET_MS_BITFIELD_LAYOUT

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()				\
  do								\
    {								\
      /* UNDER_CE without a value, as on arm-mingw32ce.  */	\
      builtin_define_std ("UNDER_CE");				\
      builtin_define ("_UNICODE");				\
      builtin_define_std ("UNICODE");				\
      /* The SH-4 defines of the eVC4 SH-4 project templates.  */ \
      builtin_define ("SHx");					\
      builtin_define ("SH4");					\
      builtin_define ("_SH4_");					\
      /* There is a single calling convention.  */		\
      builtin_define ("__stdcall=");				\
      builtin_define ("__fastcall=");				\
      builtin_define ("__cdecl=");				\
      if (!flag_iso)						\
	{							\
	  builtin_define ("_stdcall=");				\
	  builtin_define ("_fastcall=");			\
	  builtin_define ("_cdecl=");				\
	}							\
      /* Even though linkonce works with static libs, this is	\
	 needed to compare typeinfo symbols across DLLs.  */	\
      builtin_define ("__GXX_MERGED_TYPEINFO_NAMES=0");		\
      builtin_define ("__GXX_TYPEINFO_EQUALITY_INLINE=0");	\
      builtin_define ("__CEGCC_VERSION__");			\
      builtin_define ("__COREDLL__");				\
      builtin_define ("__MINGW32__");				\
      builtin_define ("__MINGW32CE__");				\
      builtin_define ("_WIN32");				\
      builtin_define_std ("WIN32");				\
      builtin_define_std ("WINNT");				\
    }								\
  while (0)

/* Types as in the Microsoft runtime.  */
#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"
#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"
#undef WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16
#undef WINT_TYPE
#define WINT_TYPE "short unsigned int"

#undef MATH_LIBRARY
#define MATH_LIBRARY ""

#undef CPP_SPEC
#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{mthreads:-D_MT}"

#undef LIB_SPEC
#define LIB_SPEC "%{pg:-lgmon} -lcoredll"

#undef LINK_SPEC
#define LINK_SPEC "%{shared|mdll: --shared} \
  %{static:-Bstatic} %{!static:-Bdynamic} \
  %{shared|mdll: -e DllMainCRTStartup}"

#undef LIBGCC_SPEC
#define LIBGCC_SPEC \
  "%{mthreads:-lmingwthrd} -lmingw32 -lgcc -lceoldname -lmingwex -lcoredll"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{shared|mdll:dllcrt3%O%s} \
  %{!shared:%{!mdll:crt3%O%s}} %{pg:gcrt3%O%s}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""

#ifndef STANDARD_STARTFILE_PREFIX_1
#define STANDARD_STARTFILE_PREFIX_1 "/mingw/lib/"
#endif
#ifndef STANDARD_STARTFILE_PREFIX_2
#define STANDARD_STARTFILE_PREFIX_2 ""
#endif

/* The caller reserves a 16-byte home area for r4..r7 below the stack
   arguments on every call, even with fewer than four arguments: call2
   does add #-16,r15 and call5 stores the fifth argument at @(16,r15)
   (msvc-sh4-abi-reference.txt).  The callee may write into it.  */
#define REG_PARM_STACK_SPACE(FNDECL) 16
#define OUTGOING_REG_PARM_STACK_SPACE(FNTYPE) 1
