/* PE/COFF support for the Windows CE SH-4 target (sh4-mingw32ce).
   Adapted from config/arm/pe.c and config/i386/winnt.c.
   Copyright (C) 1995-2020 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "function.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "stringpool.h"
#include "attribs.h"
#include "output.h"
#include "varasm.h"
#include "lto-section-names.h"

/* The function dllimport attribute is accepted and ignored, as on
   arm-mingw32ce: calls go through the jump stubs of the import library.
   dllexport is honoured by emitting -export: directives into .drectve.  */

/* Place DECL in a section of its own, for -ffunction-sections,
   -fdata-sections and one-only (COMDAT) definitions.  The linker
   merges .text$foo into .text and so on.  */

void
sh_pe_unique_section (tree decl, int reloc ATTRIBUTE_UNUSED)
{
  const char *name, *prefix;
  char *string;

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  name = targetm.strip_name_encoding (name);

  /* Don't put read-only data in .rdata$ sections; see arm/pe.c.  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    prefix = ".text$";
  else if (decl_readonly_section (decl, 0))
    prefix = ".rdata$";
  else
    prefix = ".data$";
  string = XALLOCAVEC (char, strlen (name) + strlen (prefix) + 1);
  sprintf (string, "%s%s", prefix, name);

  set_decl_section_name (decl, string);
}

#define SECTION_PE_SHARED	SECTION_MACH_DEP

unsigned int
sh_pe_section_type_flags (tree decl, const char *name ATTRIBUTE_UNUSED,
			  int reloc ATTRIBUTE_UNUSED)
{
  unsigned int flags;

  /* PE images are relocated by the loader as a whole, so relocated
     constant data can stay read-only.  */
  if (decl && TREE_CODE (decl) == FUNCTION_DECL)
    flags = SECTION_CODE;
  else if (decl && decl_readonly_section (decl, 0))
    flags = 0;
  else
    {
      flags = SECTION_WRITE;

      if (decl && TREE_CODE (decl) == VAR_DECL
	  && lookup_attribute ("shared", DECL_ATTRIBUTES (decl)))
	flags |= SECTION_PE_SHARED;
    }

  if (decl && DECL_P (decl) && DECL_ONE_ONLY (decl))
    flags |= SECTION_LINKONCE;

  return flags;
}

void
sh_pe_asm_named_section (const char *name, unsigned int flags, tree decl)
{
  char flagchars[8], *f = flagchars;

  if ((flags & (SECTION_CODE | SECTION_WRITE)) == 0)
    {
      /* Read-only data.  */
      *f++ = 'd';
      *f++ = 'r';
    }
  else
    {
      if (flags & SECTION_CODE)
	*f++ = 'x';
      if (flags & SECTION_WRITE)
	*f++ = 'w';
      if (flags & SECTION_PE_SHARED)
	*f++ = 's';
      /* No "e" (exclude) flag in this gas: mark such sections never-load.  */
      if ((flags & SECTION_EXCLUDE) != 0)
	*f++ = 'n';
    }

  /* LTO sections need 1-byte alignment to avoid confusing the
     zlib decompression algorithm with trailing zero pad bytes.  */
  if (strncmp (name, LTO_SECTION_NAME_PREFIX,
	       strlen (LTO_SECTION_NAME_PREFIX)) == 0)
    *f++ = '0';

  *f = '\0';

  fprintf (asm_out_file, "\t.section\t%s,\"%s\"\n", name, flagchars);

  if (flags & SECTION_LINKONCE)
    {
      /* Functions may have been compiled at various levels of
	 optimization, so have the linker pick one without warning.
	 selectany data is treated the same way, like the MS compiler.  */
      bool discard = (flags & SECTION_CODE)
		     || (decl && TREE_CODE (decl) != IDENTIFIER_NODE
			 && lookup_attribute ("selectany",
					      DECL_ATTRIBUTES (decl)));
      fprintf (asm_out_file, "\t.linkonce %s\n",
	       discard ? "discard" : "same_size");
    }
}

/* Names of the dllexport'd symbols of this translation unit, emitted as
   linker directives at the end of the file.  */

struct sh_pe_export
{
  const char *name;
  bool is_data;
};

static vec<sh_pe_export> sh_pe_exports;

void
sh_pe_maybe_record_exported_symbol (tree decl, const char *name, bool is_data)
{
  if (!decl || !TREE_PUBLIC (decl)
      || !lookup_attribute ("dllexport", DECL_ATTRIBUTES (decl)))
    return;

  sh_pe_export e;
  e.name = xstrdup (name);
  e.is_data = is_data;
  sh_pe_exports.safe_push (e);
}

/* Beware, DECL may be NULL if compile_file() is emitting the LTO marker.  */

void
sh_pe_asm_output_aligned_decl_common (FILE *stream, tree decl,
				      const char *name, HOST_WIDE_INT size,
				      HOST_WIDE_INT align)
{
  /* A common symbol of size zero would mean an undefined external in
     PE/COFF.  PE gas takes the alignment as a power of two.  */
  HOST_WIDE_INT rounded = size ? size : 1;

  sh_pe_maybe_record_exported_symbol (decl, name, true);

  fprintf (stream, "\t.comm\t");
  assemble_name (stream, name);
  fprintf (stream, "," HOST_WIDE_INT_PRINT_DEC ",%d\n",
	   rounded, exact_log2 (MAX (align, BITS_PER_UNIT) / BITS_PER_UNIT));
}

#include "gsyms.h"

/* Mark NAME as a function symbol (DT_FCN), as the Microsoft tools do.  */

void
sh_pe_declare_function_type (FILE *file, const char *name, int pub)
{
  fprintf (file, "\t.def\t");
  assemble_name (file, name);
  fprintf (file, ";\t.scl\t%d;\t.type\t%d;\t.endef\n",
	   pub ? (int) C_EXT : (int) C_STAT,
	   (int) DT_FCN << N_BTSHFT);
}

void
sh_pe_file_end (void)
{
  if (sh_pe_exports.is_empty ())
    return;

  fprintf (asm_out_file, "\t.section\t.drectve\n");
  for (unsigned i = 0; i < sh_pe_exports.length (); i++)
    {
      const sh_pe_export &e = sh_pe_exports[i];
      fprintf (asm_out_file, "\t.ascii \" -export:\\\"%s\\\"%s\"\n",
	       targetm.strip_name_encoding (e.name),
	       e.is_data ? ",data" : "");
    }
}

void
sh_pe_start_function (FILE *f, const char *name, tree decl)
{
  sh_pe_maybe_record_exported_symbol (decl, name, false);
  sh_pe_declare_function_type (f, name, TREE_PUBLIC (decl));
  /* In case section was altered by debugging output.  */
  if (decl != NULL_TREE)
    switch_to_section (function_section (decl));
  ASM_OUTPUT_FUNCTION_LABEL (f, name, decl);
}
