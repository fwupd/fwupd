/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fu-smbios.h"
#include "fwupd-error.h"

struct _FuSmbios {
	GObject			 parent_instance;
	gchar			*smbios_ver;
	GPtrArray		*items;
};

typedef struct __attribute__((packed)) {
	gchar			 anchor_str[4];
	guint8			 entry_point_csum;
	guint8			 entry_point_len;
	guint8			 smbios_major_ver;
	guint8			 smbios_minor_ver;
	guint16			 max_structure_sz;
	guint8			 entry_point_rev;
	guint8			 formatted_area[5];
	gchar			 intermediate_anchor_str[5];
	guint8			 intermediate_csum;
	guint16			 structure_table_len;
	guint32			 structure_table_addr;
	guint16			 number_smbios_structs;
	guint8			 smbios_bcd_rev;
} FuSmbiosStructureEntryPoint;

typedef struct __attribute__((packed)) {
	guint8			 type;
	guint8			 len;
	guint16			 handle;
} FuSmbiosStructure;

typedef struct {
	guint8			 type;
	guint16			 handle;
	GBytes			*data;
	GPtrArray		*strings;
} FuSmbiosItem;

G_DEFINE_TYPE (FuSmbios, fu_smbios, G_TYPE_OBJECT)

static gboolean
fu_smbios_setup_from_data (FuSmbios *self, const guint8 *buf, gsize sz, GError **error)
{
	/* go through each structure */
	for (gsize i = 0; i < sz; i++) {
		FuSmbiosStructure *str = (FuSmbiosStructure *) &buf[i];
		FuSmbiosItem *item;

		/* invalid */
		if (str->len == 0x00)
			break;
		if (str->len >= sz) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "structure larger than available data");
			return FALSE;
		}

		/* create a new result */
		item = g_new0 (FuSmbiosItem, 1);
		item->type = str->type;
		item->handle = str->handle;
		item->data = g_bytes_new (buf + i, str->len);
		item->strings = g_ptr_array_new_with_free_func (g_free);
		g_ptr_array_add (self->items, item);

		/* jump to the end of the struct */
		i += str->len;
		if (buf[i] == '\0' && buf[i+1] == '\0') {
			i++;
			continue;
		}

		/* add strings from table */
		for (gsize start_offset = i; i < sz; i++) {
			if (buf[i] == '\0') {
				if (start_offset == i)
					break;
				g_ptr_array_add (item->strings,
						 g_strdup ((const gchar *) &buf[start_offset]));
				start_offset = i + 1;
			}
		}
	}
	return TRUE;
}

/**
 * fu_smbios_setup_from_file:
 * @self: A #FuSmbios
 * @filename: A filename
 * @error: A #GError or %NULL
 *
 * Reads all the SMBIOS values from a DMI blob.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_smbios_setup_from_file (FuSmbios *self, const gchar *filename, GError **error)
{
	gsize sz = 0;
	g_autofree gchar *buf = NULL;
	if (!g_file_get_contents (filename, &buf, &sz, error))
		return FALSE;
	return fu_smbios_setup_from_data (self, (guint8 *) buf, sz, error);
}

/**
 * fu_smbios_setup:
 * @self: A #FuSmbios
 * @sysfsdir: A file path, e.g. '/sys/firmware' or %NULL
 * @error: A #GError or %NULL
 *
 * Reads all the SMBIOS values from the hardware.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_smbios_setup (FuSmbios *self, const gchar *sysfsdir, GError **error)
{
	FuSmbiosStructureEntryPoint *ep;
	gsize sz = 0;
	guint8 csum = 0;
	g_autofree gchar *dmi_fn = NULL;
	g_autofree gchar *dmi_raw = NULL;
	g_autofree gchar *ep_fn = NULL;
	g_autofree gchar *ep_raw = NULL;

	g_return_val_if_fail (FU_IS_SMBIOS (self), FALSE);

	/* default value */
	if (sysfsdir == NULL)
		sysfsdir = "/sys/firmware";

	/* get the smbios entry point */
	ep_fn = g_build_filename (sysfsdir, "dmi", "tables", "smbios_entry_point", NULL);
	if (!g_file_get_contents (ep_fn, &ep_raw, &sz, error))
		return FALSE;
	if (sz != sizeof(FuSmbiosStructureEntryPoint)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid smbios entry point got %" G_GSIZE_FORMAT
			     " bytes, expected %" G_GSIZE_FORMAT,
			     sz, sizeof(FuSmbiosStructureEntryPoint));
		return FALSE;
	}
	ep = (FuSmbiosStructureEntryPoint *) ep_raw;
	if (memcmp (ep->anchor_str, "_SM_", 4) != 0) {
		g_autofree gchar *tmp = g_strndup (ep->anchor_str, 4);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "anchor signature invalid, got %s", tmp);
		return FALSE;
	}
	for (guint i = 0; i < sz; i++)
		csum += ep_raw[i];
	if (csum != 0x00) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "entry point checksum invalid");
		return FALSE;
	}
	if (memcmp (ep->intermediate_anchor_str, "_DMI_", 5) != 0) {
		g_autofree gchar *tmp = g_strndup (ep->intermediate_anchor_str, 5);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "intermediate anchor signature invalid, got %s", tmp);
		return FALSE;
	}
	for (guint i = 10; i < sz; i++)
		csum += ep_raw[i];
	if (csum != 0x00) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "intermediate checksum invalid");
		return FALSE;
	}
	self->smbios_ver = g_strdup_printf ("%u.%u",
					    ep->smbios_major_ver,
					    ep->smbios_minor_ver);

	/* get the DMI data */
	dmi_fn = g_build_filename (sysfsdir, "dmi", "tables", "DMI", NULL);
	if (!g_file_get_contents (dmi_fn, &dmi_raw, &sz, error))
		return FALSE;
	if (sz != ep->structure_table_len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid DMI data size, got %" G_GSIZE_FORMAT
			     " bytes, expected %" G_GSIZE_FORMAT,
			     sz, ep->structure_table_len);
		return FALSE;
	}

	/* parse blob */
	return fu_smbios_setup_from_data (self, (guint8 *) dmi_raw, sz, error);
}

/**
 * fu_smbios_to_string:
 * @self: A #FuSmbios
 *
 * Dumps the parsed SMBIOS data to a string.
 *
 * Returns: a UTF-8 string
 **/
gchar *
fu_smbios_to_string (FuSmbios *self)
{
	GString *str;

	g_return_val_if_fail (FU_IS_SMBIOS (self), NULL);

	str = g_string_new (NULL);
	g_string_append_printf (str, "SmbiosVersion: %s\n", self->smbios_ver);
	for (guint i = 0; i < self->items->len; i++) {
		FuSmbiosItem *item = g_ptr_array_index (self->items, i);
		g_string_append_printf (str, "Type: %02x\n", item->type);
		g_string_append_printf (str, " Length: %" G_GSIZE_FORMAT "\n",
					g_bytes_get_size (item->data));
		g_string_append_printf (str, " Handle: 0x%04x\n", item->handle);
		for (guint j = 0; j < item->strings->len; j++) {
			const gchar *tmp = g_ptr_array_index (item->strings, j);
			g_string_append_printf (str, "  String[%02u]: %s\n", j, tmp);
		}
	}
	return g_string_free (str, FALSE);
}

static FuSmbiosItem *
fu_smbios_get_item_for_type (FuSmbios *self, guint8 type)
{
	for (guint i = 0; i < self->items->len; i++) {
		FuSmbiosItem *item = g_ptr_array_index (self->items, i);
		if (item->type == type)
			return item;
	}
	return NULL;
}

/**
 * fu_smbios_get_data:
 * @self: A #FuSmbios
 * @type: A structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @error: A #GError or %NULL
 *
 * Reads a SMBIOS data blob, which includes the SMBIOS section header.
 *
 * Returns: a #GBytes, or %NULL if invalid or not found
 **/
GBytes *
fu_smbios_get_data (FuSmbios *self, guint8 type, GError **error)
{
	FuSmbiosItem *item;
	g_return_val_if_fail (FU_IS_SMBIOS (self), NULL);
	item = fu_smbios_get_item_for_type (self, type);
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no structure with type %02x", type);
		return NULL;
	}
	return item->data;
}

/**
 * fu_smbios_get_string:
 * @self: A #FuSmbios
 * @type: A structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @offset: A structure offset
 * @error: A #GError or %NULL
 *
 * Reads a string from the SMBIOS string table of a specific structure.
 *
 * The @type and @offset can be referenced from the DMTF SMBIOS specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf
 *
 * Returns: a string, or %NULL if invalid or not found
 **/
const gchar *
fu_smbios_get_string (FuSmbios *self, guint8 type, guint8 offset, GError **error)
{
	FuSmbiosItem *item;
	const guint8 *data;
	gsize sz;

	g_return_val_if_fail (FU_IS_SMBIOS (self), NULL);

	/* get item */
	item = fu_smbios_get_item_for_type (self, type);
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no structure with type %02x", type);
		return NULL;
	}

	/* check offset valid */
	data = g_bytes_get_data (item->data, &sz);
	if (offset >= sz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "offset bigger than size %" G_GSIZE_FORMAT, sz);
		return NULL;
	}
	if (data[offset] == 0x00) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no data available");
		return NULL;
	}

	/* check string index valid */
	if (data[offset] > item->strings->len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "index larger than string table %u",
			     data[offset]);
		return NULL;
	}
	return g_ptr_array_index (item->strings, data[offset] - 1);
}

static void
fu_smbios_item_free (FuSmbiosItem *item)
{
	g_bytes_unref (item->data);
	g_ptr_array_unref (item->strings);
	g_free (item);
}

static void
fu_smbios_finalize (GObject *object)
{
	FuSmbios *self = FU_SMBIOS (object);
	g_free (self->smbios_ver);
	g_ptr_array_unref (self->items);
	G_OBJECT_CLASS (fu_smbios_parent_class)->finalize (object);
}

static void
fu_smbios_class_init (FuSmbiosClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_smbios_finalize;
}

static void
fu_smbios_init (FuSmbios *self)
{
	self->items = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_smbios_item_free);
}

/**
 * fu_smbios_new:
 *
 * Creates a new object to parse SMBIOS data.
 *
 * Returns: a #FuSmbios
 **/
FuSmbios *
fu_smbios_new (void)
{
	FuSmbios *self;
	self = g_object_new (FU_TYPE_SMBIOS, NULL);
	return FU_SMBIOS (self);
}
