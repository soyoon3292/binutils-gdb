/* Definitions for targets which report shared library events.

   Copyright (C) 2007-2019 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "objfiles.h"
#include "solist.h"
#include "symtab.h"
#include "symfile.h"
#include "target.h"
#include "vec.h"
#include "solib-target.h"
#include <vector>

/* Private data for each loaded library.  */
struct lm_info_target : public lm_info_base
{
  /* The library's name.  The name is normally kept in the struct
     so_list; it is only here during XML parsing.  */
  std::string name;

  /* The target can either specify segment bases or section bases, not
     both.  */

  /* The base addresses for each independently relocatable segment of
     this shared library.  */
  std::vector<CORE_ADDR> segment_bases;

  /* The base addresses for each independently allocatable,
     relocatable section of this shared library.  */
  std::vector<CORE_ADDR> section_bases;

  /* The cached offsets for each section of this shared library,
     determined from SEGMENT_BASES, or SECTION_BASES.  */
  section_offsets *offsets = NULL;
};

typedef lm_info_target *lm_info_target_p;
DEF_VEC_P(lm_info_target_p);

#if !defined(HAVE_LIBEXPAT)

static VEC(lm_info_target_p) *
solib_target_parse_libraries (const char *library)
{
  static int have_warned;

  if (!have_warned)
    {
      have_warned = 1;
      warning (_("Can not parse XML library list; XML support was disabled "
		 "at compile time"));
    }

  return NULL;
}

#else /* HAVE_LIBEXPAT */

#include "xml-support.h"

/* Handle the start of a <segment> element.  */

static void
library_list_start_segment (struct gdb_xml_parser *parser,
			    const struct gdb_xml_element *element,
			    void *user_data,
			    std::vector<gdb_xml_value> &attributes)
{
  VEC(lm_info_target_p) **list = (VEC(lm_info_target_p) **) user_data;
  lm_info_target *last = VEC_last (lm_info_target_p, *list);
  ULONGEST *address_p
    = (ULONGEST *) xml_find_attribute (attributes, "address")->value.get ();
  CORE_ADDR address = (CORE_ADDR) *address_p;

  if (!last->section_bases.empty ())
    gdb_xml_error (parser,
		   _("Library list with both segments and sections"));

  last->segment_bases.push_back (address);
}

static void
library_list_start_section (struct gdb_xml_parser *parser,
			    const struct gdb_xml_element *element,
			    void *user_data,
			    std::vector<gdb_xml_value> &attributes)
{
  VEC(lm_info_target_p) **list = (VEC(lm_info_target_p) **) user_data;
  lm_info_target *last = VEC_last (lm_info_target_p, *list);
  ULONGEST *address_p
    = (ULONGEST *) xml_find_attribute (attributes, "address")->value.get ();
  CORE_ADDR address = (CORE_ADDR) *address_p;

  if (!last->segment_bases.empty ())
    gdb_xml_error (parser,
		   _("Library list with both segments and sections"));

  last->section_bases.push_back (address);
}

/* Handle the start of a <library> element.  */

static void
library_list_start_library (struct gdb_xml_parser *parser,
			    const struct gdb_xml_element *element,
			    void *user_data,
			    std::vector<gdb_xml_value> &attributes)
{
  VEC(lm_info_target_p) **list = (VEC(lm_info_target_p) **) user_data;
  lm_info_target *item = new lm_info_target;
  item->name
    = (const char *) xml_find_attribute (attributes, "name")->value.get ();

  VEC_safe_push (lm_info_target_p, *list, item);
}

static void
library_list_end_library (struct gdb_xml_parser *parser,
			  const struct gdb_xml_element *element,
			  void *user_data, const char *body_text)
{
  VEC(lm_info_target_p) **list = (VEC(lm_info_target_p) **) user_data;
  lm_info_target *lm_info = VEC_last (lm_info_target_p, *list);

  if (lm_info->segment_bases.empty () && lm_info->section_bases.empty ())
    gdb_xml_error (parser, _("No segment or section bases defined"));
}


/* Handle the start of a <library-list> element.  */

static void
library_list_start_list (struct gdb_xml_parser *parser,
			 const struct gdb_xml_element *element,
			 void *user_data,
			 std::vector<gdb_xml_value> &attributes)
{
  struct gdb_xml_value *version = xml_find_attribute (attributes, "version");

  /* #FIXED attribute may be omitted, Expat returns NULL in such case.  */
  if (version != NULL)
    {
      const char *string = (const char *) version->value.get ();

      if (strcmp (string, "1.0") != 0)
	gdb_xml_error (parser,
		       _("Library list has unsupported version \"%s\""),
		       string);
    }
}

/* Discard the constructed library list.  */

static void
solib_target_free_library_list (void *p)
{
  VEC(lm_info_target_p) **result = (VEC(lm_info_target_p) **) p;
  lm_info_target *info;
  int ix;

  for (ix = 0; VEC_iterate (lm_info_target_p, *result, ix, info); ix++)
    delete info;

  VEC_free (lm_info_target_p, *result);
  *result = NULL;
}

/* The allowed elements and attributes for an XML library list.
   The root element is a <library-list>.  */

static const struct gdb_xml_attribute segment_attributes[] = {
  { "address", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute section_attributes[] = {
  { "address", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element library_children[] = {
  { "segment", segment_attributes, NULL,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    library_list_start_segment, NULL },
  { "section", section_attributes, NULL,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    library_list_start_section, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute library_attributes[] = {
  { "name", GDB_XML_AF_NONE, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element library_list_children[] = {
  { "library", library_attributes, library_children,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    library_list_start_library, library_list_end_library },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute library_list_attributes[] = {
  { "version", GDB_XML_AF_OPTIONAL, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element library_list_elements[] = {
  { "library-list", library_list_attributes, library_list_children,
    GDB_XML_EF_NONE, library_list_start_list, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static VEC(lm_info_target_p) *
solib_target_parse_libraries (const char *library)
{
  VEC(lm_info_target_p) *result = NULL;
  struct cleanup *back_to = make_cleanup (solib_target_free_library_list,
					  &result);

  if (gdb_xml_parse_quick (_("target library list"), "library-list.dtd",
			   library_list_elements, library, &result) == 0)
    {
      /* Parsed successfully, keep the result.  */
      discard_cleanups (back_to);
      return result;
    }

  do_cleanups (back_to);
  return NULL;
}
#endif

static struct so_list *
solib_target_current_sos (void)
{
  struct so_list *new_solib, *start = NULL, *last = NULL;
  VEC(lm_info_target_p) *library_list;
  lm_info_target *info;
  int ix;

  /* Fetch the list of shared libraries.  */
  gdb::optional<gdb::char_vector> library_document
    = target_read_stralloc (current_top_target (), TARGET_OBJECT_LIBRARIES,
			    NULL);
  if (!library_document)
    return NULL;

  /* Parse the list.  */
  library_list = solib_target_parse_libraries (library_document->data ());

  if (library_list == NULL)
    return NULL;

  /* Build a struct so_list for each entry on the list.  */
  for (ix = 0; VEC_iterate (lm_info_target_p, library_list, ix, info); ix++)
    {
      new_solib = XCNEW (struct so_list);
      strncpy (new_solib->so_name, info->name.c_str (),
	       SO_NAME_MAX_PATH_SIZE - 1);
      new_solib->so_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';
      strncpy (new_solib->so_original_name, info->name.c_str (),
	       SO_NAME_MAX_PATH_SIZE - 1);
      new_solib->so_original_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';
      new_solib->lm_info = info;

      /* We no longer need this copy of the name.  */
      info->name.clear ();

      /* Add it to the list.  */
      if (!start)
	last = start = new_solib;
      else
	{
	  last->next = new_solib;
	  last = new_solib;
	}
    }

  /* Free the library list, but not its members.  */
  VEC_free (lm_info_target_p, library_list);

  return start;
}

static void
solib_target_solib_create_inferior_hook (int from_tty)
{
  /* Nothing needed.  */
}

static void
solib_target_clear_solib (void)
{
  /* Nothing needed.  */
}

static void
solib_target_free_so (struct so_list *so)
{
  lm_info_target *li = (lm_info_target *) so->lm_info;

  gdb_assert (li->name.empty ());

  delete li;
}

static void
solib_target_relocate_section_addresses (struct so_list *so,
					 struct target_section *sec)
{
  CORE_ADDR offset;
  lm_info_target *li = (lm_info_target *) so->lm_info;

  /* Build the offset table only once per object file.  We can not do
     it any earlier, since we need to open the file first.  */
  if (li->offsets == NULL)
    {
      int num_sections = gdb_bfd_count_sections (so->abfd);

      li->offsets
	= ((struct section_offsets *)
	   xzalloc (SIZEOF_N_SECTION_OFFSETS (num_sections)));

      if (!li->section_bases.empty ())
	{
	  int i;
	  asection *sect;
	  int num_alloc_sections = 0;

	  for (i = 0, sect = so->abfd->sections;
	       sect != NULL;
	       i++, sect = sect->next)
	    if ((bfd_get_section_flags (so->abfd, sect) & SEC_ALLOC))
	      num_alloc_sections++;

	  if (num_alloc_sections != li->section_bases.size ())
	    warning (_("\
Could not relocate shared library \"%s\": wrong number of ALLOC sections"),
		     so->so_name);
	  else
	    {
	      int bases_index = 0;
	      int found_range = 0;

	      so->addr_low = ~(CORE_ADDR) 0;
	      so->addr_high = 0;
	      for (i = 0, sect = so->abfd->sections;
		   sect != NULL;
		   i++, sect = sect->next)
		{
		  if (!(bfd_get_section_flags (so->abfd, sect) & SEC_ALLOC))
		    continue;
		  if (bfd_section_size (so->abfd, sect) > 0)
		    {
		      CORE_ADDR low, high;

		      low = li->section_bases[i];
		      high = low + bfd_section_size (so->abfd, sect) - 1;

		      if (low < so->addr_low)
			so->addr_low = low;
		      if (high > so->addr_high)
			so->addr_high = high;
		      gdb_assert (so->addr_low <= so->addr_high);
		      found_range = 1;
		    }
		  li->offsets->offsets[i] = li->section_bases[bases_index];
		  bases_index++;
		}
	      if (!found_range)
		so->addr_low = so->addr_high = 0;
	      gdb_assert (so->addr_low <= so->addr_high);
	    }
	}
      else if (!li->segment_bases.empty ())
	{
	  struct symfile_segment_data *data;

	  data = get_symfile_segment_data (so->abfd);
	  if (data == NULL)
	    warning (_("\
Could not relocate shared library \"%s\": no segments"), so->so_name);
	  else
	    {
	      ULONGEST orig_delta;
	      int i;

	      if (!symfile_map_offsets_to_segments (so->abfd, data, li->offsets,
						    li->segment_bases.size (),
						    li->segment_bases.data ()))
		warning (_("\
Could not relocate shared library \"%s\": bad offsets"), so->so_name);

	      /* Find the range of addresses to report for this library in
		 "info sharedlibrary".  Report any consecutive segments
		 which were relocated as a single unit.  */
	      gdb_assert (li->segment_bases.size () > 0);
	      orig_delta = li->segment_bases[0] - data->segment_bases[0];

	      for (i = 1; i < data->num_segments; i++)
		{
		  /* If we have run out of offsets, assume all
		     remaining segments have the same offset.  */
		  if (i >= li->segment_bases.size ())
		    continue;

		  /* If this segment does not have the same offset, do
		     not include it in the library's range.  */
		  if (li->segment_bases[i] - data->segment_bases[i]
		      != orig_delta)
		    break;
		}

	      so->addr_low = li->segment_bases[0];
	      so->addr_high = (data->segment_bases[i - 1]
			       + data->segment_sizes[i - 1]
			       + orig_delta);
	      gdb_assert (so->addr_low <= so->addr_high);

	      free_symfile_segment_data (data);
	    }
	}
    }

  offset = li->offsets->offsets[gdb_bfd_section_index
			        (sec->the_bfd_section->owner,
				 sec->the_bfd_section)];
  sec->addr += offset;
  sec->endaddr += offset;
}

static int
solib_target_open_symbol_file_object (int from_tty)
{
  /* We can't locate the main symbol file based on the target's
     knowledge; the user has to specify it.  */
  return 0;
}

static int
solib_target_in_dynsym_resolve_code (CORE_ADDR pc)
{
  /* We don't have a range of addresses for the dynamic linker; there
     may not be one in the program's address space.  So only report
     PLT entries (which may be import stubs).  */
  return in_plt_section (pc);
}

struct target_so_ops solib_target_so_ops;

void
_initialize_solib_target (void)
{
  solib_target_so_ops.relocate_section_addresses
    = solib_target_relocate_section_addresses;
  solib_target_so_ops.free_so = solib_target_free_so;
  solib_target_so_ops.clear_solib = solib_target_clear_solib;
  solib_target_so_ops.solib_create_inferior_hook
    = solib_target_solib_create_inferior_hook;
  solib_target_so_ops.current_sos = solib_target_current_sos;
  solib_target_so_ops.open_symbol_file_object
    = solib_target_open_symbol_file_object;
  solib_target_so_ops.in_dynsym_resolve_code
    = solib_target_in_dynsym_resolve_code;
  solib_target_so_ops.bfd_open = solib_bfd_open;

  /* Set current_target_so_ops to solib_target_so_ops if not already
     set.  */
  if (current_target_so_ops == 0)
    current_target_so_ops = &solib_target_so_ops;
}
