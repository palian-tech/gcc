/* Definitions of target machine for GNU compiler, for SH PE/COFF
   (Windows CE).
   Copyright (C) 1995-2020 Free Software Foundation, Inc.

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

#define TARGET_SH_PE 1

#undef TARGET_PECOFF
#define TARGET_PECOFF 1

#define TARGET_EXECUTABLE_SUFFIX ".exe"

/* Symbols carry a leading underscore, like the Microsoft SH compiler
   (_call2, __divlu).  */
#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(STRING, PREFIX, NUM) \
  sprintf ((STRING), "*%s%s%ld", LOCAL_LABEL_PREFIX, (PREFIX), (long)(NUM))

#undef ASM_COMMENT_START
#define ASM_COMMENT_START "!"

/* Debugging: DWARF 2.  */
#define DWARF2_DEBUGGING_INFO 1
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG
#undef DWARF2_UNWIND_INFO
#define DWARF2_UNWIND_INFO 0
#define DBX_REGISTER_NUMBER(REGNO) SH_DBX_REGISTER_NUMBER (REGNO)

/* FIXME: DWARF offsets should be section-relative (.secrel32), which SH
   gas does not support yet; plain .long offsets are emitted instead, so
   debug info in linked images is not usable by debuggers.  */

/* Sections.  */
#undef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP	"\t.bss"
#undef READONLY_DATA_SECTION_ASM_OP
#define READONLY_DATA_SECTION_ASM_OP	"\t.section\t.rdata,\"dr\""

/* Constructors are run by libgcc's __main from __CTOR_LIST__ and
   __DTOR_LIST__, which the PE linker script provides; drop the sh.h
   versions that use __ctors and __dtors.  */
#undef DO_GLOBAL_CTORS_BODY
#undef DO_GLOBAL_DTORS_BODY

#define CTORS_SECTION_ASM_OP	"\t.section\t.ctors,\"w\""
#define DTORS_SECTION_ASM_OP	"\t.section\t.dtors,\"w\""

#define SUPPORTS_ONE_ONLY 1
#define MULTIPLE_SYMBOL_SPACES 1

#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  sh_pe_asm_named_section
#define TARGET_SECTION_TYPE_FLAGS  sh_pe_section_type_flags
#define TARGET_ASM_UNIQUE_SECTION sh_pe_unique_section
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section


#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL) \
  sh_pe_start_function (FILE, NAME, DECL)

#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME(STREAM, NAME, DECL)	\
  do {							\
    sh_pe_maybe_record_exported_symbol (DECL, NAME, true); \
    ASM_OUTPUT_LABEL ((STREAM), (NAME));		\
  } while (0)

#undef ASM_OUTPUT_ALIGNED_DECL_COMMON
#define ASM_OUTPUT_ALIGNED_DECL_COMMON(STREAM, DECL, NAME, SIZE, ALIGN) \
  sh_pe_asm_output_aligned_decl_common (STREAM, DECL, NAME, SIZE, ALIGN)

/* Local common: emit into .bss, which keeps the alignment.  */
#undef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_DECL_LOCAL(STREAM, DECL, NAME, SIZE, ALIGN)	\
  do {									\
    switch_to_section (bss_section);					\
    ASM_OUTPUT_ALIGN ((STREAM), floor_log2 ((ALIGN) / BITS_PER_UNIT));	\
    ASM_OUTPUT_LABEL ((STREAM), (NAME));				\
    ASM_OUTPUT_SKIP ((STREAM), (SIZE) ? (SIZE) : 1);			\
  } while (0)

#undef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

#define ASM_OUTPUT_SKIP(FILE, SIZE) \
  fprintf ((FILE), "\t.space\t" HOST_WIDE_INT_PRINT_UNSIGNED "\n", \
	   (unsigned HOST_WIDE_INT) (SIZE))

/* GNU as supports weak symbols on PE/COFF.  */
#define ASM_WEAKEN_LABEL(FILE, NAME)	\
  do					\
    {					\
      fputs ("\t.weak\t", (FILE));	\
      assemble_name ((FILE), (NAME));	\
      fputc ('\n', (FILE));		\
    }					\
  while (0)

/* Output STRING, a file name, with backslashes turned into slashes.  */
#undef OUTPUT_QUOTED_STRING
#define OUTPUT_QUOTED_STRING(FILE, STRING)		\
do {							\
  const char *_string = (const char *) (STRING);	\
  char c;						\
							\
  putc ('\"', (FILE));					\
  while ((c = *_string++) != 0)				\
    {							\
      if (c == '\\')					\
	c = '/';					\
      if (ISPRINT (c))					\
	{						\
	  if (c == '\"')				\
	    putc ('\\', (FILE));			\
	  putc (c, (FILE));				\
	}						\
      else						\
	fprintf ((FILE), "\\%03o", (unsigned char) c);	\
    }							\
  putc ('\"', (FILE));					\
} while (0)
