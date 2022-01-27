/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSmbios"

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-kenv.h"
#include "fu-smbios-private.h"

/**
 * FuSmbios:
 *
 * Enumerate the SMBIOS data on the system, either using DMI or Device Tree.
 *
 * See also: [class@FuHwids]
 */

struct _FuSmbios {
	FuFirmware parent_instance;
	guint32 structure_table_len;
	GPtrArray *items;
};

/* little endian */
typedef struct __attribute__((packed)) {
	gchar anchor_str[4];
	guint8 entry_point_csum;
	guint8 entry_point_len;
	guint8 smbios_major_ver;
	guint8 smbios_minor_ver;
	guint16 max_structure_sz;
	guint8 entry_point_rev;
	guint8 formatted_area[5];
	gchar intermediate_anchor_str[5];
	guint8 intermediate_csum;
	guint16 structure_table_len;
	guint32 structure_table_addr;
	guint16 number_smbios_structs;
	guint8 smbios_bcd_rev;
} FuSmbiosStructureEntryPoint32;

/* little endian */
typedef struct __attribute__((packed)) {
	gchar anchor_str[5];
	guint8 entry_point_csum;
	guint8 entry_point_len;
	guint8 smbios_major_ver;
	guint8 smbios_minor_ver;
	guint8 smbios_docrev;
	guint8 entry_point_rev;
	guint8 reserved0;
	guint32 structure_table_len;
	guint64 structure_table_addr;
} FuSmbiosStructureEntryPoint64;

typedef struct {
	guint8 type;
	guint16 handle;
	GByteArray *buf;
	GPtrArray *strings;
} FuSmbiosItem;

G_DEFINE_TYPE(FuSmbios, fu_smbios, FU_TYPE_FIRMWARE)

static void
fu_smbios_set_integer(FuSmbios *self, guint8 type, guint8 offset, guint8 value)
{
	FuSmbiosItem *item = g_ptr_array_index(self->items, type);
	for (guint i = item->buf->len; i < (guint)offset + 1; i++)
		fu_byte_array_append_uint8(item->buf, 0x0);
	item->buf->data[offset] = value;
}

static void
fu_smbios_set_string(FuSmbios *self, guint8 type, guint8 offset, const gchar *buf, gssize bufsz)
{
	FuSmbiosItem *item = g_ptr_array_index(self->items, type);

	/* NUL terminated UTF-8 */
	if (bufsz < 0)
		bufsz = strlen(buf);

	/* add value to string table */
	g_ptr_array_add(item->strings, g_strndup(buf, (gsize)bufsz));
	fu_smbios_set_integer(self, type, offset, item->strings->len);
}

static gboolean
fu_smbios_convert_dt_string(FuSmbios *self,
			    guint8 type,
			    guint8 offset,
			    const gchar *path,
			    const gchar *subpath)
{
	gsize bufsz = 0;
	g_autofree gchar *fn = g_build_filename(path, subpath, NULL);
	g_autofree gchar *buf = NULL;

	/* not found */
	if (!g_file_get_contents(fn, &buf, &bufsz, NULL))
		return FALSE;
	if (bufsz == 0)
		return FALSE;
	fu_smbios_set_string(self, type, offset, buf, (gssize)bufsz);
	return TRUE;
}

static gchar **
fu_smbios_convert_dt_string_array(FuSmbios *self, const gchar *path, const gchar *subpath)
{
	gsize bufsz = 0;
	g_autofree gchar *fn = g_build_filename(path, subpath, NULL);
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) split = NULL;

	/* not found */
	if (!g_file_get_contents(fn, &buf, &bufsz, NULL))
		return NULL;
	if (bufsz == 0)
		return NULL;

	/* return only if valid */
	split = g_strsplit(buf, ",", -1);
	if (g_strv_length(split) == 0)
		return NULL;

	/* success */
	return g_steal_pointer(&split);
}

#ifdef HAVE_KENV_H

static gboolean
fu_smbios_convert_kenv_string(FuSmbios *self,
			      guint8 type,
			      guint8 offset,
			      const gchar *sminfo,
			      GError **error)
{
	g_autofree gchar *value = fu_kenv_get_string(sminfo, error);
	if (value == NULL)
		return FALSE;
	fu_smbios_set_string(self, type, offset, value, -1);
	return TRUE;
}

static gboolean
fu_smbios_setup_from_kenv(FuSmbios *self, GError **error)
{
	gboolean is_valid = FALSE;
	g_autoptr(GError) error_local = NULL;

	/* add all four faked structures */
	for (guint i = 0; i < FU_SMBIOS_STRUCTURE_TYPE_LAST; i++) {
		FuSmbiosItem *item = g_new0(FuSmbiosItem, 1);
		item->type = i;
		item->buf = g_byte_array_new();
		item->strings = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(self->items, item);
	}

	/* DMI:Manufacturer */
	if (!fu_smbios_convert_kenv_string(self,
					   FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
					   0x04,
					   "smbios.bios.vendor",
					   &error_local)) {
		g_debug("ignoring: %s", error_local->message);
		g_clear_error(&error_local);
	} else {
		is_valid = TRUE;
	}

	/* DMI:BiosVersion */
	if (!fu_smbios_convert_kenv_string(self,
					   FU_SMBIOS_STRUCTURE_TYPE_BIOS,
					   0x05,
					   "smbios.bios.version",
					   &error_local)) {
		g_debug("ignoring: %s", error_local->message);
		g_clear_error(&error_local);
	} else {
		is_valid = TRUE;
	}

	/* DMI:Family */
	if (!fu_smbios_convert_kenv_string(self,
					   FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
					   0x1a,
					   "smbios.system.family",
					   &error_local)) {
		g_debug("ignoring: %s", error_local->message);
		g_clear_error(&error_local);
	} else {
		is_valid = TRUE;
	}

	/* DMI:ProductName */
	if (!fu_smbios_convert_kenv_string(self,
					   FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
					   0x05,
					   "smbios.planar.product",
					   &error_local)) {
		g_debug("ignoring: %s", error_local->message);
		g_clear_error(&error_local);
	} else {
		is_valid = TRUE;
	}

	/* DMI:BaseboardManufacturer */
	if (!fu_smbios_convert_kenv_string(self,
					   FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD,
					   0x04,
					   "smbios.planar.maker",
					   &error_local)) {
		g_debug("ignoring: %s", error_local->message);
		g_clear_error(&error_local);
	} else {
		is_valid = TRUE;
	}

	/* we got no data */
	if (!is_valid) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "no SMBIOS information provided");
		return FALSE;
	}

	/* success */
	return TRUE;
}
#endif

static gboolean
fu_smbios_setup_from_path_dt(FuSmbios *self, const gchar *path, GError **error)
{
	gboolean has_family;
	gboolean has_model;
	gboolean has_vendor;
	g_autofree gchar *fn_battery = NULL;

	/* add all four faked structures */
	for (guint i = 0; i < FU_SMBIOS_STRUCTURE_TYPE_LAST; i++) {
		FuSmbiosItem *item = g_new0(FuSmbiosItem, 1);
		item->type = i;
		item->buf = g_byte_array_new();
		item->strings = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(self->items, item);
	}

	/* if it has a battery it is portable (probably a laptop) */
	fn_battery = g_build_filename(path, "battery", NULL);
	if (g_file_test(fn_battery, G_FILE_TEST_EXISTS)) {
		fu_smbios_set_integer(self,
				      FU_SMBIOS_STRUCTURE_TYPE_CHASSIS,
				      0x05,
				      FU_SMBIOS_CHASSIS_KIND_PORTABLE);
	}

	/* DMI:Manufacturer */
	has_vendor = fu_smbios_convert_dt_string(self,
						 FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
						 0x04,
						 path,
						 "vendor");

	/* DMI:Family */
	has_family = fu_smbios_convert_dt_string(self,
						 FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
						 0x1a,
						 path,
						 "model-name");

	/* DMI:ProductName */
	has_model =
	    fu_smbios_convert_dt_string(self, FU_SMBIOS_STRUCTURE_TYPE_SYSTEM, 0x05, path, "model");

	/* fall back to the first compatible string if required */
	if (!has_vendor || !has_model || !has_family) {
		g_auto(GStrv) parts = NULL;

		/* NULL if invalid, otherwise we're sure this has size of exactly 3 */
		parts = fu_smbios_convert_dt_string_array(self, path, "compatible");
		if (parts != NULL) {
			if (!has_vendor && g_strv_length(parts) > 0) {
				fu_smbios_set_string(self,
						     FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
						     0x4,
						     parts[0],
						     -1);
			}
			if (!has_model && g_strv_length(parts) > 1) {
				fu_smbios_set_string(self,
						     FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
						     0x05,
						     parts[1],
						     -1);
			}
			if (!has_family && g_strv_length(parts) > 2) {
				fu_smbios_set_string(self,
						     FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
						     0x1a,
						     parts[2],
						     -1);
			}
		}
	}

	/* DMI:BiosVersion */
	fu_smbios_convert_dt_string(self,
				    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
				    0x05,
				    path,
				    "ibm,firmware-versions/version");

	/* DMI:BaseboardManufacturer */
	fu_smbios_convert_dt_string(self,
				    FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD,
				    0x04,
				    path,
				    "vpd/root-node-vpd@a000/enclosure@1e00/backplane@800/vendor");

	/* DMI:BaseboardProduct */
	fu_smbios_convert_dt_string(
	    self,
	    FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD,
	    0x05,
	    path,
	    "vpd/root-node-vpd@a000/enclosure@1e00/backplane@800/part-number");

	return TRUE;
}

static gboolean
fu_smbios_setup_from_data(FuSmbios *self, const guint8 *buf, gsize sz, GError **error)
{
	/* go through each structure */
	for (gsize i = 0; i < sz; i++) {
		FuSmbiosItem *item;
		guint16 str_handle = 0;
		guint8 str_len = 0;
		guint8 str_type = 0;

		/* le */
		if (!fu_common_read_uint8_safe(buf, sz, i + 0x0, &str_type, error))
			return FALSE;
		if (!fu_common_read_uint8_safe(buf, sz, i + 0x1, &str_len, error))
			return FALSE;
		if (!fu_common_read_uint16_safe(buf,
						sz,
						i + 0x2,
						&str_handle,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;

		/* invalid */
		if (str_len == 0x00)
			break;
		if (i + str_len >= sz) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "structure larger than available data");
			return FALSE;
		}

		/* create a new result */
		item = g_new0(FuSmbiosItem, 1);
		item->type = str_type;
		item->handle = GUINT16_FROM_LE(str_handle);
		item->buf = g_byte_array_sized_new(str_len);
		item->strings = g_ptr_array_new_with_free_func(g_free);
		g_byte_array_append(item->buf, buf + i, str_len);
		g_ptr_array_add(self->items, item);

		/* jump to the end of the struct */
		i += str_len;
		if (buf[i] == '\0' && buf[i + 1] == '\0') {
			i++;
			continue;
		}

		/* add strings from table */
		for (gsize start_offset = i; i < sz; i++) {
			if (buf[i] == '\0') {
				if (start_offset == i)
					break;
				g_ptr_array_add(item->strings,
						g_strdup((const gchar *)&buf[start_offset]));
				start_offset = i + 1;
			}
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
	g_autofree gchar *basename = NULL;

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* use a heuristic */
	basename = g_path_get_basename(filename);
	if (g_strcmp0(basename, "base") == 0)
		return fu_smbios_setup_from_path_dt(self, filename, error);

	/* DMI blob */
	if (!g_file_get_contents(filename, &buf, &sz, error))
		return FALSE;
	return fu_smbios_setup_from_data(self, (guint8 *)buf, sz, error);
}

static gboolean
fu_smbios_encode_string_from_kernel(FuSmbios *self,
				    const gchar *file_contents,
				    guint8 type,
				    guint8 offset,
				    GError **error)
{
	fu_smbios_set_string(self, type, offset, file_contents, -1);
	return TRUE;
}

static gboolean
fu_smbios_encode_byte_from_kernel(FuSmbios *self,
				  const gchar *file_contents,
				  guint8 type,
				  guint8 offset,
				  GError **error)
{
	gchar *endp;
	gint64 value = g_ascii_strtoll(file_contents, &endp, 10);

	if (*endp != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "non-numeric values in numeric string: %s",
			    endp);
		return FALSE;
	}
	if (value < 0 || value > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "value \"%s\" is not representable in a byte",
			    file_contents);
		return FALSE;
	}

	fu_smbios_set_integer(self, type, offset, value);
	return TRUE;
}

/*
 * The mapping from SMBIOS field to sysfs name can be found by mapping
 * the field to a kernel property name in dmi_decode()
 * (drivers/firmware/dmi_scan.c), then the property name to sysfs entry
 * in dmi_id_init_attr_table() (drivers/firmware/dmi-id.c). This table
 * lists each attribute exposed in /sys/class/dmi when CONFIG_DMIID is
 * enabled, mapping to the SMBIOS field and a function that can convert
 * the textual version of the field back into the raw SMBIOS table
 * representation.
 */
#define SYSFS_DMI_FIELD(_name, _type, _offset, kind)                                               \
	{                                                                                          \
		.name = _name, .type = _type, .offset = _offset,                                   \
		.encode = fu_smbios_encode_##kind##_from_kernel                                    \
	}
const struct kernel_dmi_field {
	const gchar *name;
	gboolean (*encode)(FuSmbios *, const gchar *, guint8, guint8, GError **);
	guint8 type;
	guint8 offset;
} KERNEL_DMI_FIELDS[] = {
    SYSFS_DMI_FIELD("bios_vendor", 0, 4, string),
    SYSFS_DMI_FIELD("bios_version", 0, 5, string),
    SYSFS_DMI_FIELD("bios_date", 0, 8, string),
    SYSFS_DMI_FIELD("sys_vendor", 1, 4, string),
    SYSFS_DMI_FIELD("product_name", 1, 5, string),
    SYSFS_DMI_FIELD("product_version", 1, 6, string),
    SYSFS_DMI_FIELD("product_serial", 1, 7, string),
    /* SYSFS_DMI_FIELD("product_uuid", 1, 8, uuid) */
    SYSFS_DMI_FIELD("product_family", 1, 26, string),
    SYSFS_DMI_FIELD("product_sku", 1, 25, string),
    SYSFS_DMI_FIELD("board_vendor", 2, 4, string),
    SYSFS_DMI_FIELD("board_name", 2, 5, string),
    SYSFS_DMI_FIELD("board_version", 2, 6, string),
    SYSFS_DMI_FIELD("board_serial", 2, 7, string),
    SYSFS_DMI_FIELD("board_asset_tag", 2, 8, string),
    SYSFS_DMI_FIELD("chassis_vendor", 3, 4, string),
    SYSFS_DMI_FIELD("chassis_type", 3, 5, byte),
    SYSFS_DMI_FIELD("chassis_version", 3, 6, string),
    SYSFS_DMI_FIELD("chassis_serial", 3, 7, string),
    SYSFS_DMI_FIELD("chassis_asset_tag", 3, 8, string),
};

/**
 * fu_smbios_setup_from_kernel:
 * @self: a #FuSmbios
 * @path: a directory path
 * @error: (nullable): optional return location for an error
 *
 * Reads SMBIOS value from DMI values provided by the kernel, such as in
 * /sys/class/dmi on Linux.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.2
 **/
gboolean
fu_smbios_setup_from_kernel(FuSmbios *self, const gchar *path, GError **error)
{
	gboolean any_success = FALSE;

	/* add fake structures */
	for (guint i = 0; i < FU_SMBIOS_STRUCTURE_TYPE_LAST; i++) {
		FuSmbiosItem *item = g_new0(FuSmbiosItem, 1);
		item->type = i;
		item->buf = g_byte_array_new();
		item->strings = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(self->items, item);
	}

	/* parse every known field from the corresponding file */
	for (gsize i = 0; i < G_N_ELEMENTS(KERNEL_DMI_FIELDS); i++) {
		const struct kernel_dmi_field *field = &KERNEL_DMI_FIELDS[i];
		gsize bufsz = 0;
		g_autofree gchar *buf = NULL;
		g_autofree gchar *fn = g_build_filename(path, field->name, NULL);
		g_autoptr(GError) local_error = NULL;

		if (!g_file_get_contents(fn, &buf, &bufsz, &local_error)) {
			g_debug("unable to read SMBIOS data from %s: %s", fn, local_error->message);
			continue;
		}

		/* trim trailing newline added by kernel */
		if (buf[bufsz - 1] == '\n')
			buf[bufsz - 1] = 0;

		if (!field->encode(self, buf, field->type, field->offset, &local_error)) {
			g_warning("failed to parse SMBIOS data from %s: %s",
				  fn,
				  local_error->message);
			continue;
		}

		any_success = TRUE;
	}
	if (!any_success) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read any SMBIOS values from %s",
			    path);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_smbios_parse_ep32(FuSmbios *self, const gchar *buf, gsize sz, GError **error)
{
	FuSmbiosStructureEntryPoint32 *ep;
	guint8 csum = 0;
	g_autofree gchar *version_str = NULL;

	/* verify size */
	if (sz != sizeof(FuSmbiosStructureEntryPoint32)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid smbios entry point got %" G_GSIZE_FORMAT
			    " bytes, expected %" G_GSIZE_FORMAT,
			    sz,
			    sizeof(FuSmbiosStructureEntryPoint32));
		return FALSE;
	}

	/* verify checksum */
	for (guint i = 0; i < sz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "entry point checksum invalid");
		return FALSE;
	}

	/* verify intermediate section */
	ep = (FuSmbiosStructureEntryPoint32 *)buf;
	if (memcmp(ep->intermediate_anchor_str, "_DMI_", 5) != 0) {
		g_autofree gchar *tmp = g_strndup(ep->intermediate_anchor_str, 5);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "intermediate anchor signature invalid, got %s",
			    tmp);
		return FALSE;
	}
	for (guint i = 10; i < sz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "intermediate checksum invalid");
		return FALSE;
	}
	self->structure_table_len = GUINT16_FROM_LE(ep->structure_table_len);
	version_str = g_strdup_printf("%u.%u", ep->smbios_major_ver, ep->smbios_minor_ver);
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);
	fu_firmware_set_version_raw(FU_FIRMWARE(self),
				    (((guint16)ep->smbios_major_ver) << 8) + ep->smbios_minor_ver);
	return TRUE;
}

static gboolean
fu_smbios_parse_ep64(FuSmbios *self, const gchar *buf, gsize sz, GError **error)
{
	FuSmbiosStructureEntryPoint64 *ep;
	guint8 csum = 0;
	g_autofree gchar *version_str = NULL;

	/* verify size */
	if (sz != sizeof(FuSmbiosStructureEntryPoint64)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid smbios3 entry point got %" G_GSIZE_FORMAT
			    " bytes, expected %" G_GSIZE_FORMAT,
			    sz,
			    sizeof(FuSmbiosStructureEntryPoint32));
		return FALSE;
	}

	/* verify checksum */
	for (guint i = 0; i < sz; i++)
		csum += buf[i];
	if (csum != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "entry point checksum invalid");
		return FALSE;
	}
	ep = (FuSmbiosStructureEntryPoint64 *)buf;
	self->structure_table_len = GUINT32_FROM_LE(ep->structure_table_len);
	version_str = g_strdup_printf("%u.%u", ep->smbios_major_ver, ep->smbios_minor_ver);
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);
	return TRUE;
}

static gboolean
fu_smbios_setup_from_path_dmi(FuSmbios *self, const gchar *path, GError **error)
{
	gsize sz = 0;
	g_autofree gchar *dmi_fn = NULL;
	g_autofree gchar *dmi_raw = NULL;
	g_autofree gchar *ep_fn = NULL;
	g_autofree gchar *ep_raw = NULL;

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);

	/* get the smbios entry point */
	ep_fn = g_build_filename(path, "smbios_entry_point", NULL);
	if (!g_file_get_contents(ep_fn, &ep_raw, &sz, error))
		return FALSE;

	/* check we got enough data to read the signature */
	if (sz < 5) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid smbios entry point got %" G_GSIZE_FORMAT
			    " bytes, expected %" G_GSIZE_FORMAT " or %" G_GSIZE_FORMAT,
			    sz,
			    sizeof(FuSmbiosStructureEntryPoint32),
			    sizeof(FuSmbiosStructureEntryPoint64));
		return FALSE;
	}

	/* parse 32 bit structure */
	if (memcmp(ep_raw, "_SM_", 4) == 0) {
		if (!fu_smbios_parse_ep32(self, ep_raw, sz, error))
			return FALSE;
	} else if (memcmp(ep_raw, "_SM3_", 5) == 0) {
		if (!fu_smbios_parse_ep64(self, ep_raw, sz, error))
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
	if (sz != self->structure_table_len) {
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
		guint64 addr_start,
		guint64 addr_end,
		FwupdInstallFlags flags,
		GError **error)
{
	FuSmbios *self = FU_SMBIOS(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	return fu_smbios_setup_from_data(self, buf, bufsz, error);
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
	g_autofree gchar *basename = NULL;

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* use a heuristic */
	basename = g_path_get_basename(path);
	if (g_strcmp0(basename, "base") == 0)
		return fu_smbios_setup_from_path_dt(self, path, error);
	return fu_smbios_setup_from_path_dmi(self, path, error);
}

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
	g_autofree gchar *path = NULL;
	g_autofree gchar *path_dt = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	const gchar *path_dmi_class = "/sys/class/dmi/id";

	g_return_val_if_fail(FU_IS_SMBIOS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	sysfsfwdir = fu_common_get_path(FU_PATH_KIND_SYSFSDIR_FW);

	/* DMI */
	path = g_build_filename(sysfsfwdir, "dmi", "tables", NULL);
	if (g_file_test(path, G_FILE_TEST_EXISTS)) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_smbios_setup_from_path(self, path, &error_local)) {
			if (!g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_ACCES)) {
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
			g_debug("ignoring %s", error_local->message);
		}
	}

	/* the values the kernel parsed; these are world-readable */
	if (g_file_test(path_dmi_class, G_FILE_TEST_IS_DIR)) {
		g_debug("trying to read %s", path_dmi_class);
		return fu_smbios_setup_from_kernel(self, path_dmi_class, error);
	}

	/* DT */
	path_dt = g_build_filename(sysfsfwdir, "devicetree", "base", NULL);
	if (g_file_test(path_dt, G_FILE_TEST_EXISTS))
		return fu_smbios_setup_from_path(self, path_dt, error);

#ifdef HAVE_KENV_H
	/* kenv */
	return fu_smbios_setup_from_kenv(self, error);
#endif

	/* neither found */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "neither SMBIOS or DT found");
	return FALSE;
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
			g_autofree gchar *value = fu_common_strsafe(tmp, 20);
			xb_builder_node_insert_text(bc, "string", value, "idx", title, NULL);
		}
	}
}

/**
 * fu_smbios_to_string:
 * @self: a #FuSmbios
 *
 * Dumps the parsed SMBIOS data to a string.
 *
 * Returns: a UTF-8 string
 *
 * Since: 1.0.0
 **/
gchar *
fu_smbios_to_string(FuSmbios *self)
{
	g_return_val_if_fail(FU_IS_SMBIOS(self), NULL);
	return fu_firmware_to_string(FU_FIRMWARE(self));
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
