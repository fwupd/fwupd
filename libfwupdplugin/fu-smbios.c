/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSmbios"

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#ifdef _WIN32
#include <errhandlingapi.h>
#include <sysinfoapi.h>
#endif

#include "fwupd-error.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-path.h"
#include "fu-smbios-private.h"
#include "fu-smbios-struct.h"
#include "fu-string.h"

/**
 * FuSmbios:
 *
 * Enumerate the SMBIOS data on the system.
 *
 * See also: [class@FuHwids]
 */

struct _FuSmbios {
	FuFirmware parent_instance;
	guint32 structure_table_len;
	GPtrArray *items;
};

typedef struct {
	guint8 type;
	guint16 handle;
	GByteArray *buf;
	GPtrArray *strings;
} FuSmbiosItem;

G_DEFINE_TYPE(FuSmbios, fu_smbios, FU_TYPE_FIRMWARE)

static gboolean
fu_smbios_setup_from_data(FuSmbios *self, const guint8 *buf, gsize bufsz, GError **error)
{
	/* go through each structure */
	for (gsize i = 0; i < bufsz; i++) {
		FuSmbiosItem *item;
		guint8 length;
		g_autoptr(GByteArray) st_str = NULL;

		/* sanity check */
		st_str = fu_struct_smbios_structure_parse(buf, bufsz, i, error);
		if (st_str == NULL)
			return FALSE;
		length = fu_struct_smbios_structure_get_length(st_str);
		if (length < st_str->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "structure smaller than allowed @0x%x",
				    (guint)i);
			return FALSE;
		}
		if (i + length >= bufsz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "structure larger than available data @0x%x",
				    (guint)i);
			return FALSE;
		}

		/* create a new result */
		item = g_new0(FuSmbiosItem, 1);
		item->type = fu_struct_smbios_structure_get_type(st_str);
		item->handle = fu_struct_smbios_structure_get_handle(st_str);
		item->buf = g_byte_array_sized_new(length);
		item->strings = g_ptr_array_new_with_free_func(g_free);
		g_byte_array_append(item->buf, buf + i, length);
		g_ptr_array_add(self->items, item);

		/* jump to the end of the formatted area of the struct */
		i += length;

		/* add strings from table */
		while (i < bufsz) {
			GString *str;

			/* end of string section */
			if (item->strings->len > 0 && buf[i] == 0x0)
				break;

			/* copy into string table */
			str = fu_strdup((const gchar *)buf, bufsz, i);
			i += str->len + 1;
			g_ptr_array_add(item->strings, g_string_free(str, FALSE));
		}
	}
	return TRUE;
}

/**
 * fu_smbios_setup_from_file:
 * @self: a #FuSmbios
 * @filename: a filename
 * @error: (nullable): optional return location for an error
 *
 * Reads all the SMBIOS values from a DMI blob.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.0
 **/
gboolean
fu_smbios_setup_from_file(FuSmbios *self, const gchar *filename, GError **error)
{
	gsize sz = 0;
	g_autofree gchar *buf = NULL;

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* DMI blob */
	if (!g_file_get_contents(filename, &buf, &sz, error))
		return FALSE;
	return fu_smbios_setup_from_data(self, (guint8 *)buf, sz, error);
}

static gboolean
fu_smbios_parse_ep32(FuSmbios *self, const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 csum = 0;
	g_autofree gchar *version_str = NULL;
	g_autofree gchar *intermediate_anchor_str = NULL;
	g_autoptr(GByteArray) st_ep32 = NULL;

	/* verify checksum */
	st_ep32 = fu_struct_smbios_ep32_parse(buf, bufsz, 0x0, error);
	if (st_ep32 == NULL)
		return FALSE;
	for (guint i = 0; i < bufsz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "entry point checksum invalid");
		return FALSE;
	}

	/* verify intermediate section */
	intermediate_anchor_str = fu_struct_smbios_ep32_get_intermediate_anchor_str(st_ep32);
	if (g_strcmp0(intermediate_anchor_str, "_DMI_") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "intermediate anchor signature invalid, got %s",
			    intermediate_anchor_str);
		return FALSE;
	}
	for (guint i = 10; i < bufsz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "intermediate checksum invalid");
		return FALSE;
	}
	self->structure_table_len = fu_struct_smbios_ep32_get_structure_table_len(st_ep32);
	version_str = g_strdup_printf("%u.%u",
				      fu_struct_smbios_ep32_get_smbios_major_ver(st_ep32),
				      fu_struct_smbios_ep32_get_smbios_minor_ver(st_ep32));
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);
	fu_firmware_set_version_raw(
	    FU_FIRMWARE(self),
	    (((guint16)fu_struct_smbios_ep32_get_smbios_major_ver(st_ep32)) << 8) +
		fu_struct_smbios_ep32_get_smbios_minor_ver(st_ep32));
	return TRUE;
}

static gboolean
fu_smbios_parse_ep64(FuSmbios *self, const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 csum = 0;
	g_autofree gchar *version_str = NULL;
	g_autoptr(GByteArray) st_ep64 = NULL;

	/* verify checksum */
	st_ep64 = fu_struct_smbios_ep64_parse(buf, bufsz, 0x0, error);
	if (st_ep64 == NULL)
		return FALSE;
	for (guint i = 0; i < bufsz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "entry point checksum invalid");
		return FALSE;
	}
	self->structure_table_len = fu_struct_smbios_ep64_get_structure_table_len(st_ep64);
	version_str = g_strdup_printf("%u.%u",
				      fu_struct_smbios_ep64_get_smbios_major_ver(st_ep64),
				      fu_struct_smbios_ep64_get_smbios_minor_ver(st_ep64));
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);
	return TRUE;
}

/**
 * fu_smbios_setup_from_path:
 * @self: a #FuSmbios
 * @path: a path, e.g. `/sys/firmware/dmi/tables`
 * @error: (nullable): optional return location for an error
 *
 * Reads all the SMBIOS values from a specific path.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.0
 **/
gboolean
fu_smbios_setup_from_path(FuSmbios *self, const gchar *path, GError **error)
{
	gsize sz = 0;
	g_autofree gchar *dmi_fn = NULL;
	g_autofree gchar *dmi_raw = NULL;
	g_autofree gchar *ep_fn = NULL;
	g_autofree gchar *ep_raw = NULL;

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get the smbios entry point */
	ep_fn = g_build_filename(path, "smbios_entry_point", NULL);
	if (!g_file_get_contents(ep_fn, &ep_raw, &sz, error))
		return FALSE;

	/* check we got enough data to read the signature */
	if (sz < 5) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid smbios entry point got 0x%x bytes, expected 0x%x or 0x%x",
			    (guint)sz,
			    (guint)FU_STRUCT_SMBIOS_EP32_SIZE,
			    (guint)FU_STRUCT_SMBIOS_EP64_SIZE);
		return FALSE;
	}

	/* parse 32 bit structure */
	if (memcmp(ep_raw, "_SM_", 4) == 0) {
		if (!fu_smbios_parse_ep32(self, (const guint8 *)ep_raw, sz, error))
			return FALSE;
	} else if (memcmp(ep_raw, "_SM3_", 5) == 0) {
		if (!fu_smbios_parse_ep64(self, (const guint8 *)ep_raw, sz, error))
			return FALSE;
	} else {
		g_autofree gchar *tmp = g_strndup(ep_raw, 4);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "SMBIOS signature invalid, got %s",
			    tmp);
		return FALSE;
	}

	/* get the DMI data */
	dmi_fn = g_build_filename(path, "DMI", NULL);
	if (!g_file_get_contents(dmi_fn, &dmi_raw, &sz, error))
		return FALSE;
	if (sz > self->structure_table_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid DMI data size, got %" G_GSIZE_FORMAT
			    " bytes, expected %" G_GUINT32_FORMAT,
			    sz,
			    self->structure_table_len);
		return FALSE;
	}

	/* parse blob */
	return fu_smbios_setup_from_data(self, (guint8 *)dmi_raw, sz, error);
}

static gboolean
fu_smbios_parse(FuFirmware *firmware,
		GBytes *fw,
		gsize offset,
		FwupdInstallFlags flags,
		GError **error)
{
	FuSmbios *self = FU_SMBIOS(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	return fu_smbios_setup_from_data(self, buf, bufsz, error);
}

#ifdef _WIN32
#define FU_SMBIOS_FT_SIG_ACPI	0x41435049
#define FU_SMBIOS_FT_SIG_FIRM	0x4649524D
#define FU_SMBIOS_FT_SIG_RSMB	0x52534D42
#define FU_SMBIOS_FT_RAW_OFFSET 0x08
#endif

/**
 * fu_smbios_setup:
 * @self: a #FuSmbios
 * @error: (nullable): optional return location for an error
 *
 * Reads all the SMBIOS values from the hardware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.0
 **/
gboolean
fu_smbios_setup(FuSmbios *self, GError **error)
{
#ifdef _WIN32
	gsize bufsz;
	guint rc;
	g_autofree guint8 *buf = NULL;

	rc = GetSystemFirmwareTable(FU_SMBIOS_FT_SIG_RSMB, 0, 0, 0);
	if (rc <= 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to access RSMB [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (rc < FU_SMBIOS_FT_RAW_OFFSET || rc > 0x1000000) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "RSMB impossible size");
		return FALSE;
	}
	bufsz = rc;
	buf = g_malloc0(bufsz);
	rc = GetSystemFirmwareTable(FU_SMBIOS_FT_SIG_RSMB, 0, buf, (DWORD)bufsz);
	if (rc <= 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to read RSMB [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	return fu_smbios_setup_from_data(self,
					 buf + FU_SMBIOS_FT_RAW_OFFSET,
					 bufsz - FU_SMBIOS_FT_RAW_OFFSET,
					 error);
#else
	g_autofree gchar *path = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* DMI */
	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	path = g_build_filename(sysfsfwdir, "dmi", "tables", NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "SMBIOS tables not found at %s",
			    path);
		return FALSE;
	}
	if (!fu_smbios_setup_from_path(self, path, &error_local)) {
		if (!g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_ACCES)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring %s", error_local->message);
	}

	/* success */
	return TRUE;
#endif
}

static void
fu_smbios_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuSmbios *self = FU_SMBIOS(firmware);

	for (guint i = 0; i < self->items->len; i++) {
		FuSmbiosItem *item = g_ptr_array_index(self->items, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "item", NULL);
		fu_xmlb_builder_insert_kx(bc, "type", item->type);
		fu_xmlb_builder_insert_kx(bc, "length", item->buf->len);
		fu_xmlb_builder_insert_kx(bc, "handle", item->handle);
		for (guint j = 0; j < item->strings->len; j++) {
			const gchar *tmp = g_ptr_array_index(item->strings, j);
			g_autofree gchar *title = g_strdup_printf("%02u", j);
			g_autofree gchar *value = fu_strsafe(tmp, 20);
			xb_builder_node_insert_text(bc, "string", value, "idx", title, NULL);
		}
	}
}

static FuSmbiosItem *
fu_smbios_get_item_for_type(FuSmbios *self, guint8 type)
{
	for (guint i = 0; i < self->items->len; i++) {
		FuSmbiosItem *item = g_ptr_array_index(self->items, i);
		if (item->type == type)
			return item;
	}
	return NULL;
}

/**
 * fu_smbios_get_data:
 * @self: a #FuSmbios
 * @type: a structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @error: (nullable): optional return location for an error
 *
 * Reads a SMBIOS data blob, which includes the SMBIOS section header.
 *
 * Returns: (transfer full): a #GBytes, or %NULL if invalid or not found
 *
 * Since: 1.0.0
 **/
GBytes *
fu_smbios_get_data(FuSmbios *self, guint8 type, GError **error)
{
	FuSmbiosItem *item;

	g_return_val_if_fail(FU_IS_SMBIOS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	item = fu_smbios_get_item_for_type(self, type);
	if (item == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no structure with type %02x",
			    type);
		return NULL;
	}
	return g_bytes_new(item->buf->data, item->buf->len);
}

/**
 * fu_smbios_get_integer:
 * @self: a #FuSmbios
 * @type: a structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @offset: a structure offset
 * @error: (nullable): optional return location for an error
 *
 * Reads an integer value from the SMBIOS string table of a specific structure.
 *
 * The @type and @offset can be referenced from the DMTF SMBIOS specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf
 *
 * Returns: an integer, or %G_MAXUINT if invalid or not found
 *
 * Since: 1.5.0
 **/
guint
fu_smbios_get_integer(FuSmbios *self, guint8 type, guint8 offset, GError **error)
{
	FuSmbiosItem *item;

	g_return_val_if_fail(FU_IS_SMBIOS(self), 0);
	g_return_val_if_fail(error == NULL || *error == NULL, 0);

	/* get item */
	item = fu_smbios_get_item_for_type(self, type);
	if (item == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no structure with type %02x",
			    type);
		return G_MAXUINT;
	}

	/* check offset valid */
	if (offset >= item->buf->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "offset bigger than size %u",
			    item->buf->len);
		return G_MAXUINT;
	}

	/* success */
	return item->buf->data[offset];
}

/**
 * fu_smbios_get_string:
 * @self: a #FuSmbios
 * @type: a structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @offset: a structure offset
 * @error: (nullable): optional return location for an error
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
fu_smbios_get_string(FuSmbios *self, guint8 type, guint8 offset, GError **error)
{
	FuSmbiosItem *item;

	g_return_val_if_fail(FU_IS_SMBIOS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get item */
	item = fu_smbios_get_item_for_type(self, type);
	if (item == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no structure with type %02x",
			    type);
		return NULL;
	}

	/* check offset valid */
	if (offset >= item->buf->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "offset bigger than size %u",
			    item->buf->len);
		return NULL;
	}
	if (item->buf->data[offset] == 0x00) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no data available");
		return NULL;
	}

	/* check string index valid */
	if (item->buf->data[offset] > item->strings->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "index larger than string table %u",
			    item->strings->len);
		return NULL;
	}
	return g_ptr_array_index(item->strings, item->buf->data[offset] - 1);
}

static void
fu_smbios_item_free(FuSmbiosItem *item)
{
	g_byte_array_unref(item->buf);
	g_ptr_array_unref(item->strings);
	g_free(item);
}

static void
fu_smbios_finalize(GObject *object)
{
	FuSmbios *self = FU_SMBIOS(object);
	g_ptr_array_unref(self->items);
	G_OBJECT_CLASS(fu_smbios_parent_class)->finalize(object);
}

static void
fu_smbios_class_init(FuSmbiosClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_smbios_finalize;
	klass_firmware->parse = fu_smbios_parse;
	klass_firmware->export = fu_smbios_export;
}

static void
fu_smbios_init(FuSmbios *self)
{
	self->items = g_ptr_array_new_with_free_func((GDestroyNotify)fu_smbios_item_free);
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
fu_smbios_new(void)
{
	FuSmbios *self;
	self = g_object_new(FU_TYPE_SMBIOS, NULL);
	return FU_SMBIOS(self);
}
