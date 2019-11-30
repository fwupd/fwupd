/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuSmbios"

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fu-common.h"
#include "fu-smbios-private.h"
#include "fwupd-error.h"

struct _FuSmbios {
	GObject			 parent_instance;
	gchar			*smbios_ver;
	guint32			 structure_table_len;
	GPtrArray		*items;
};

/* little endian */
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
} FuSmbiosStructureEntryPoint32;

/* little endian */
typedef struct __attribute__((packed)) {
	gchar			 anchor_str[5];
	guint8			 entry_point_csum;
	guint8			 entry_point_len;
	guint8			 smbios_major_ver;
	guint8			 smbios_minor_ver;
	guint8			 smbios_docrev;
	guint8			 entry_point_rev;
	guint8			 reserved0;
	guint32			 structure_table_len;
	guint64			 structure_table_addr;
} FuSmbiosStructureEntryPoint64;

/* little endian */
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
		item->handle = GUINT16_FROM_LE (str->handle);
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
 *
 * Since: 1.0.0
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

static gboolean
fu_smbios_parse_ep32 (FuSmbios *self, const gchar *buf, gsize sz, GError **error)
{
	FuSmbiosStructureEntryPoint32 *ep;
	guint8 csum = 0;

	/* verify size */
	if (sz != sizeof(FuSmbiosStructureEntryPoint32)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid smbios entry point got %" G_GSIZE_FORMAT
			     " bytes, expected %" G_GSIZE_FORMAT,
			     sz, sizeof(FuSmbiosStructureEntryPoint32));
		return FALSE;
	}

	/* verify checksum */
	for (guint i = 0; i < sz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "entry point checksum invalid");
		return FALSE;
	}

	/* verify intermediate section */
	ep = (FuSmbiosStructureEntryPoint32 *) buf;
	if (memcmp (ep->intermediate_anchor_str, "_DMI_", 5) != 0) {
		g_autofree gchar *tmp = g_strndup (ep->intermediate_anchor_str, 5);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "intermediate anchor signature invalid, got %s", tmp);
		return FALSE;
	}
	for (guint i = 10; i < sz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "intermediate checksum invalid");
		return FALSE;
	}
	self->structure_table_len = GUINT16_FROM_LE (ep->structure_table_len);
	self->smbios_ver = g_strdup_printf ("%u.%u",
					    ep->smbios_major_ver,
					    ep->smbios_minor_ver);
	return TRUE;
}

static gboolean
fu_smbios_parse_ep64 (FuSmbios *self, const gchar *buf, gsize sz, GError **error)
{
	FuSmbiosStructureEntryPoint64 *ep;
	guint8 csum = 0;

	/* verify size */
	if (sz != sizeof(FuSmbiosStructureEntryPoint64)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid smbios3 entry point got %" G_GSIZE_FORMAT
			     " bytes, expected %" G_GSIZE_FORMAT,
			     sz, sizeof(FuSmbiosStructureEntryPoint32));
		return FALSE;
	}

	/* verify checksum */
	for (guint i = 0; i < sz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "entry point checksum invalid");
		return FALSE;
	}
	ep = (FuSmbiosStructureEntryPoint64 *) buf;
	self->structure_table_len = GUINT32_FROM_LE (ep->structure_table_len);
	self->smbios_ver = g_strdup_printf ("%u.%u",
					    ep->smbios_major_ver,
					    ep->smbios_minor_ver);
	return TRUE;
}

/**
 * fu_smbios_setup_from_path:
 * @self: A #FuSmbios
 * @path: A path, e.g. `/sys/firmware/dmi/tables`
 * @error: A #GError or %NULL
 *
 * Reads all the SMBIOS values from a specific path.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.0
 **/
gboolean
fu_smbios_setup_from_path (FuSmbios *self, const gchar *path, GError **error)
{
	gsize sz = 0;
	g_autofree gchar *dmi_fn = NULL;
	g_autofree gchar *dmi_raw = NULL;
	g_autofree gchar *ep_fn = NULL;
	g_autofree gchar *ep_raw = NULL;

	g_return_val_if_fail (FU_IS_SMBIOS (self), FALSE);

	/* get the smbios entry point */
	ep_fn = g_build_filename (path, "smbios_entry_point", NULL);
	if (!g_file_get_contents (ep_fn, &ep_raw, &sz, error))
		return FALSE;

	/* check we got enough data to read the signature */
	if (sz < 5) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid smbios entry point got %" G_GSIZE_FORMAT
			     " bytes, expected %" G_GSIZE_FORMAT
			     " or %" G_GSIZE_FORMAT, sz,
			     sizeof(FuSmbiosStructureEntryPoint32),
			     sizeof(FuSmbiosStructureEntryPoint64));
		return FALSE;
	}

	/* parse 32 bit structure */
	if (memcmp (ep_raw, "_SM_", 4) == 0) {
		if (!fu_smbios_parse_ep32 (self, ep_raw, sz, error))
			return FALSE;
	} else if (memcmp (ep_raw, "_SM3_", 5) == 0) {
		if (!fu_smbios_parse_ep64 (self, ep_raw, sz, error))
			return FALSE;
	} else {
		g_autofree gchar *tmp = g_strndup (ep_raw, 4);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "SMBIOS signature invalid, got %s", tmp);
		return FALSE;
	}

	/* get the DMI data */
	dmi_fn = g_build_filename (path, "DMI", NULL);
	if (!g_file_get_contents (dmi_fn, &dmi_raw, &sz, error))
		return FALSE;
	if (sz != self->structure_table_len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid DMI data size, got %" G_GSIZE_FORMAT
			     " bytes, expected %" G_GUINT32_FORMAT,
			     sz, self->structure_table_len);
		return FALSE;
	}

	/* parse blob */
	return fu_smbios_setup_from_data (self, (guint8 *) dmi_raw, sz, error);
}

/**
 * fu_smbios_setup:
 * @self: A #FuSmbios
 * @error: A #GError or %NULL
 *
 * Reads all the SMBIOS values from the hardware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.0
 **/
gboolean
fu_smbios_setup (FuSmbios *self, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_return_val_if_fail (FU_IS_SMBIOS (self), FALSE);
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	path = g_build_filename (sysfsfwdir, "dmi", "tables", NULL);
	return fu_smbios_setup_from_path (self, path, error);
}

/**
 * fu_smbios_to_string:
 * @self: A #FuSmbios
 *
 * Dumps the parsed SMBIOS data to a string.
 *
 * Returns: a UTF-8 string
 *
 * Since: 1.0.0
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
 * Returns: (transfer full): a #GBytes, or %NULL if invalid or not found
 *
 * Since: 1.0.0
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
	return g_bytes_ref (item->data);
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
 *
 * Since: 1.0.0
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
 *
 * Since: 1.0.0
 **/
FuSmbios *
fu_smbios_new (void)
{
	FuSmbios *self;
	self = g_object_new (FU_TYPE_SMBIOS, NULL);
	return FU_SMBIOS (self);
}
