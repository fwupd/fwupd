/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-crc.h"
#include "fu-dump.h"
#include "fu-fdt-firmware.h"
#include "fu-fdt-image.h"
#include "fu-mem.h"

/**
 * FuFdtFirmware:
 *
 * A Flattened DeviceTree firmware image.
 *
 * Documented:
 * https://devicetree-specification.readthedocs.io/en/latest/chapter5-flattened-format.html
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint32 cpuid;
} FuFdtFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFdtFirmware, fu_fdt_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_fdt_firmware_get_instance_private(o))

typedef struct __attribute__((packed)) {
	guint32 magic;
	guint32 totalsize;
	guint32 off_dt_struct;
	guint32 off_dt_strings;
	guint32 off_mem_rsvmap;
	guint32 version;
	guint32 last_comp_version;
	guint32 boot_cpuid_phys;
	guint32 size_dt_strings;
	guint32 size_dt_struct;
} FuFdtHeader;

typedef struct __attribute__((packed)) {
	guint64 address;
	guint64 size;
} FuFdtReserveEntry;

typedef struct __attribute__((packed)) {
	guint32 len;
	guint32 nameoff;
} FuFdtProp;

#define FDT_MAGIC      0xD00DFEED
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP	       0x00000004
#define FDT_END	       0x00000009

#define FDT_LAST_COMP_VERSION 2

static GString *
fu_string_new_safe(const guint8 *buf, gsize bufsz, gsize offset, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);
	for (gsize i = offset; i < bufsz; i++) {
		if (buf[i] == '\0')
			return g_steal_pointer(&str);
		g_string_append_c(str, (gchar)buf[i]);
	}
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "buffer not NULL terminated");
	return NULL;
}

static void
fu_fdt_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFdtFirmware *self = FU_FDT_FIRMWARE(firmware);
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "cpuid", priv->cpuid);
}

/**
 * fu_fdt_firmware_get_cpuid:
 * @self: a #FuFdtFirmware
 *
 * Gets the CPUID.
 *
 * Returns: integer
 *
 * Since: 1.8.2
 **/
guint32
fu_fdt_firmware_get_cpuid(FuFdtFirmware *self)
{
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FDT_FIRMWARE(self), 0x0);
	return priv->cpuid;
}

/**
 * fu_fdt_firmware_set_cpuid:
 * @self: a #FuFdtFirmware
 * @cpuid: integer value
 *
 * Sets the CPUID.
 *
 * Since: 1.8.2
 **/
void
fu_fdt_firmware_set_cpuid(FuFdtFirmware *self, guint32 cpuid)
{
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FDT_FIRMWARE(self));
	priv->cpuid = cpuid;
}

/**
 * fu_fdt_firmware_get_image_by_path:
 * @self: a #FuFdtFirmware
 * @path: ID path, e.g. `/images/firmware-1`
 * @error: (nullable): optional return location for an error
 *
 * Gets the FDT image for a specific path.
 *
 * Returns: (transfer full): a #FuFirmware, or %NULL
 *
 * Since: 1.8.2
 **/
FuFdtImage *
fu_fdt_firmware_get_image_by_path(FuFdtFirmware *self, const gchar *path, GError **error)
{
	g_auto(GStrv) paths = NULL;
	g_autoptr(FuFirmware) img_current = g_object_ref(FU_FIRMWARE(self));

	g_return_val_if_fail(FU_IS_FDT_FIRMWARE(self), NULL);
	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(path[0] != '\0', NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	paths = g_strsplit(path, "/", -1);
	for (guint i = 0; paths[i] != NULL; i++) {
		const gchar *id = paths[i];
		g_autoptr(FuFirmware) img_tmp = NULL;

		/* special case for empty */
		if (id[0] == '\0')
			id = NULL;
		img_tmp = fu_firmware_get_image_by_id(img_current, id, error);
		if (img_tmp == NULL)
			return NULL;
		g_set_object(&img_current, img_tmp);
	}

	/* success */
	return FU_FDT_IMAGE(g_steal_pointer(&img_current));
}

static gboolean
fu_fdt_firmware_parse_dt_struct(FuFdtFirmware *self, GBytes *fw, GHashTable *strtab, GError **error)
{
	gsize bufsz = 0;
	gsize offset = 0;
	gboolean has_end = FALSE;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(FuFirmware) firmware_current = g_object_ref(FU_FIRMWARE(self));

	/* debug */
	if (g_getenv("FU_FDT_FIRMWARE_VERBOSE") != NULL)
		fu_dump_bytes(G_LOG_DOMAIN, "dt_struct", fw);

	/* parse */
	while (offset < bufsz) {
		guint32 token = 0;

		/* read tag from aligned offset */
		offset = fu_common_align_up(offset, FU_FIRMWARE_ALIGNMENT_4);
		if (!fu_memread_uint32_safe(buf, bufsz, offset, &token, G_BIG_ENDIAN, error))
			return FALSE;
		if (g_getenv("FU_FDT_FIRMWARE_VERBOSE") != NULL)
			g_debug("token: 0x%x", token);
		offset += sizeof(guint32);

		/* nothing to do */
		if (token == FDT_NOP)
			continue;

		/* END */
		if (token == FDT_END) {
			if (firmware_current != FU_FIRMWARE(self)) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "got END with unclosed node");
				return FALSE;
			}
			has_end = TRUE;
			break;
		}

		/* BEGIN NODE */
		if (token == FDT_BEGIN_NODE) {
			g_autoptr(GString) str = NULL;
			g_autoptr(FuFirmware) image = NULL;
			str = fu_string_new_safe(buf, bufsz, offset, error);
			if (str == NULL)
				return FALSE;
			offset += str->len + 1;
			image = fu_fdt_image_new();
			if (str->len > 0)
				fu_firmware_set_id(image, str->str);
			fu_firmware_set_offset(image, offset);
			fu_firmware_add_image(firmware_current, image);
			g_set_object(&firmware_current, image);
			continue;
		}

		/* END NODE */
		if (token == FDT_END_NODE) {
			if (firmware_current == FU_FIRMWARE(self)) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "got END NODE with no node to end");
				return FALSE;
			}
			g_set_object(&firmware_current, fu_firmware_get_parent(firmware_current));
			continue;
		}

		/* PROP */
		if (token == FDT_PROP) {
			guint32 prop_len = 0;
			guint32 prop_nameoff = 0;
			gpointer value = NULL;
			g_autoptr(GBytes) blob = NULL;

			/* sanity check */
			if (firmware_current == FU_FIRMWARE(self)) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "got PROP with unopen node");
				return FALSE;
			}

			/* parse */
			if (!fu_memread_uint32_safe(buf,
						    bufsz,
						    offset + G_STRUCT_OFFSET(FuFdtProp, len),
						    &prop_len,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			if (!fu_memread_uint32_safe(buf,
						    bufsz,
						    offset + G_STRUCT_OFFSET(FuFdtProp, nameoff),
						    &prop_nameoff,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			offset += sizeof(FuFdtProp);

			/* add property */
			if (!g_hash_table_lookup_extended(strtab,
							  GINT_TO_POINTER(prop_nameoff),
							  NULL,
							  &value)) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "invalid strtab offset 0x%x",
					    prop_nameoff);
				return FALSE;
			}
			blob = fu_bytes_new_offset(fw, offset, prop_len, error);
			if (blob == NULL)
				return FALSE;
			fu_fdt_image_set_attr(FU_FDT_IMAGE(firmware_current),
					      (const gchar *)value,
					      blob);
			offset += prop_len;
			continue;
		}

		/* unknown token */
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid token 0x%x @0%x",
			    token,
			    (guint)offset);
		return FALSE;
	}

	/* did not see FDT_END */
	if (!has_end) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "did not see FDT_END");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fdt_firmware_parse_dt_strings(FuFdtFirmware *self,
				 GBytes *fw,
				 GHashTable *strtab,
				 GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* debug */
	if (g_getenv("FU_FDT_FIRMWARE_VERBOSE") != NULL)
		fu_dump_bytes(G_LOG_DOMAIN, "dt_strings", fw);

	/* parse */
	for (gsize offset = 0; offset < bufsz; offset++) {
		g_autoptr(GString) str = NULL;
		str = fu_string_new_safe(buf, bufsz, offset, error);
		if (str == NULL)
			return FALSE;
		if (str->len == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "strtab invalid @0x%x",
				    (guint)offset);
			return FALSE;
		}
		if (g_getenv("FU_FDT_FIRMWARE_VERBOSE") != NULL)
			g_debug("strtab: %s", str->str);
		g_hash_table_insert(strtab, GINT_TO_POINTER(offset), g_strdup(str->str));
		offset += str->len;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fdt_firmware_parse_mem_rsvmap(FuFdtFirmware *self,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 GError **error)
{
	/* parse */
	for (; offset < bufsz; offset += sizeof(FuFdtReserveEntry)) {
		guint64 address = 0;
		guint64 size = 0;
		if (!fu_memread_uint64_safe(buf,
					    bufsz,
					    offset + G_STRUCT_OFFSET(FuFdtReserveEntry, address),
					    &address,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint64_safe(buf,
					    bufsz,
					    offset + G_STRUCT_OFFSET(FuFdtReserveEntry, size),
					    &size,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if (g_getenv("FU_FDT_FIRMWARE_VERBOSE") != NULL)
			g_debug("mem_rsvmap: 0x%x, 0x%x", (guint)address, (guint)size);
		if (address == 0x0 && size == 0x0)
			break;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fdt_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint32 magic = 0;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset + G_STRUCT_OFFSET(FuFdtHeader, magic),
				    &magic,
				    G_BIG_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != FDT_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid magic 0x%x, expected 0x%x",
			    magic,
			    (guint)FDT_MAGIC);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fdt_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuFdtFirmware *self = FU_FDT_FIRMWARE(firmware);
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	guint32 totalsize = 0;
	gsize bufsz = 0;
	guint32 off_dt_struct = 0;
	guint32 off_dt_strings = 0;
	guint32 off_mem_rsvmap = 0;
	guint32 version = 0;
	guint32 last_comp_version = 0;
	guint32 size_dt_strings = 0;
	guint32 size_dt_struct = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GHashTable) strtab = NULL; /* uint:str */

	/* sanity check */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, totalsize),
				    &totalsize,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (totalsize > bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "truncated image, got 0x%x, expected >= 0x%x",
			    (guint)bufsz,
			    (guint)totalsize);
		return FALSE;
	}
	fu_firmware_set_size(firmware, totalsize);

	/* read string table */
	strtab = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, off_dt_strings),
				    &off_dt_strings,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, size_dt_strings),
				    &size_dt_strings,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (size_dt_strings != 0x0) {
		g_autoptr(GBytes) dt_strings = NULL;
		dt_strings =
		    fu_bytes_new_offset(fw, offset + off_dt_strings, size_dt_strings, error);
		if (dt_strings == NULL)
			return FALSE;
		if (!fu_fdt_firmware_parse_dt_strings(self, dt_strings, strtab, error))
			return FALSE;
	}

	/* read out DT struct */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, off_dt_struct),
				    &off_dt_struct,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, size_dt_struct),
				    &size_dt_struct,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (size_dt_struct != 0x0) {
		g_autoptr(GBytes) dt_struct = NULL;
		dt_struct = fu_bytes_new_offset(fw, offset + off_dt_struct, size_dt_struct, error);
		if (dt_struct == NULL)
			return FALSE;
		if (!fu_fdt_firmware_parse_dt_struct(self, dt_struct, strtab, error))
			return FALSE;
	}

	/* read out reserved map */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, off_mem_rsvmap),
				    &off_mem_rsvmap,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (off_mem_rsvmap != 0x0) {
		if (!fu_fdt_firmware_parse_mem_rsvmap(self,
						      buf,
						      bufsz,
						      offset + off_mem_rsvmap,
						      error))
			return FALSE;
	}

	/* read in CPUID */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, boot_cpuid_phys),
				    &priv->cpuid,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;

	/* header version */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, last_comp_version),
				    &last_comp_version,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	if (last_comp_version < FDT_LAST_COMP_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid header version, got 0x%x, expected >= 0x%x",
			    (guint)last_comp_version,
			    (guint)FDT_LAST_COMP_VERSION);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuFdtHeader, version),
				    &version,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, version);
	return TRUE;
}

typedef struct {
	GByteArray *dt_strings;
	GByteArray *dt_struct;
	GHashTable *strtab;
} FuFdtFirmwareBuildHelper;

static guint32
fu_fdt_firmware_append_to_strtab(FuFdtFirmwareBuildHelper *helper, const gchar *key)
{
	gpointer tmp = NULL;
	guint32 offset;

	/* already exists */
	if (g_hash_table_lookup_extended(helper->strtab, key, NULL, &tmp))
		return GPOINTER_TO_UINT(tmp);

	if (g_getenv("FU_FDT_FIRMWARE_VERBOSE") != NULL)
		g_debug("adding strtab: %s", key);
	offset = helper->dt_strings->len;
	g_byte_array_append(helper->dt_strings, (const guint8 *)key, strlen(key));
	fu_byte_array_append_uint8(helper->dt_strings, 0x0);
	g_hash_table_insert(helper->strtab, g_strdup(key), GUINT_TO_POINTER(offset));
	return offset;
}

static gboolean
fu_fdt_firmware_write_image(FuFdtImage *img,
			    FuFdtFirmwareBuildHelper *helper,
			    guint depth,
			    GError **error)
{
	const gchar *id = fu_firmware_get_id(FU_FIRMWARE(img));
	g_autoptr(GPtrArray) images = fu_firmware_get_images(FU_FIRMWARE(img));
	g_autoptr(GPtrArray) attrs = fu_fdt_image_get_attrs(img);

	/* sanity check */
	if (depth > 0 && id == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "child FuFdtImage requires ID");
		return FALSE;
	}

	/* BEGIN_NODE, ID, NUL */
	fu_byte_array_append_uint32(helper->dt_struct, FDT_BEGIN_NODE, G_BIG_ENDIAN);
	if (id != NULL) {
		g_byte_array_append(helper->dt_struct, (const guint8 *)id, strlen(id) + 1);
	} else {
		fu_byte_array_append_uint8(helper->dt_struct, 0x0);
	}
	fu_byte_array_align_up(helper->dt_struct, FU_FIRMWARE_ALIGNMENT_4, 0x0);

	/* write properties */
	for (guint i = 0; i < attrs->len; i++) {
		const gchar *key = g_ptr_array_index(attrs, i);
		g_autoptr(GBytes) blob = NULL;

		blob = fu_fdt_image_get_attr(img, key, error);
		if (blob == NULL)
			return FALSE;
		fu_byte_array_append_uint32(helper->dt_struct, FDT_PROP, G_BIG_ENDIAN);
		fu_byte_array_append_uint32(helper->dt_struct,
					    g_bytes_get_size(blob),
					    G_BIG_ENDIAN);
		fu_byte_array_append_uint32(helper->dt_struct,
					    fu_fdt_firmware_append_to_strtab(helper, key),
					    G_BIG_ENDIAN);
		fu_byte_array_append_bytes(helper->dt_struct, blob);
		fu_byte_array_align_up(helper->dt_struct, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	}

	/* write children, recursively */
	for (guint i = 0; i < images->len; i++) {
		FuFdtImage *img_child = g_ptr_array_index(images, i);
		if (!fu_fdt_firmware_write_image(img_child, helper, depth + 1, error))
			return FALSE;
	}

	/* END_NODE */
	fu_byte_array_append_uint32(helper->dt_struct, FDT_END_NODE, G_BIG_ENDIAN);
	return TRUE;
}

static GBytes *
fu_fdt_firmware_write(FuFirmware *firmware, GError **error)
{
	FuFdtFirmware *self = FU_FDT_FIRMWARE(firmware);
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	guint32 off_dt_struct;
	guint32 off_dt_strings;
	guint32 off_mem_rsvmap;
	guint32 totalsize;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) dt_strings = g_byte_array_new();
	g_autoptr(GByteArray) dt_struct = g_byte_array_new();
	g_autoptr(GByteArray) mem_rsvmap = g_byte_array_new();
	g_autoptr(GHashTable) strtab = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	FuFdtFirmwareBuildHelper helper = {
	    .dt_strings = dt_strings,
	    .dt_struct = dt_struct,
	    .strtab = strtab,
	};

	/* mem_rsvmap */
	off_mem_rsvmap = fu_common_align_up(sizeof(FuFdtHeader), FU_FIRMWARE_ALIGNMENT_4);
	fu_byte_array_append_uint64(mem_rsvmap, 0x0, G_BIG_ENDIAN);
	fu_byte_array_append_uint64(mem_rsvmap, 0x0, G_BIG_ENDIAN);

	/* dt_struct */
	off_dt_struct =
	    fu_common_align_up(off_mem_rsvmap + mem_rsvmap->len, FU_FIRMWARE_ALIGNMENT_4);

	/* only one root node supported */
	if (images->len != 1) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no root node");
		return NULL;
	}
	if (!fu_fdt_firmware_write_image(FU_FDT_IMAGE(g_ptr_array_index(images, 0)),
					 &helper,
					 0,
					 error))
		return NULL;
	fu_byte_array_append_uint32(dt_struct, FDT_END, G_BIG_ENDIAN);

	/* dt_strings */
	off_dt_strings =
	    fu_common_align_up(off_dt_struct + dt_struct->len, FU_FIRMWARE_ALIGNMENT_4);

	/* write header */
	totalsize = off_dt_strings + dt_strings->len;
	fu_byte_array_append_uint32(buf, FDT_MAGIC, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, totalsize, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, off_dt_struct, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, off_dt_strings, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, off_mem_rsvmap, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, fu_firmware_get_version_raw(firmware), G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, FDT_LAST_COMP_VERSION, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, priv->cpuid, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, dt_strings->len, G_BIG_ENDIAN);
	fu_byte_array_append_uint32(buf, dt_struct->len, G_BIG_ENDIAN);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_4, 0x0);

	/* write mem_rsvmap, dt_struct, dt_strings */
	g_byte_array_append(buf, mem_rsvmap->data, mem_rsvmap->len);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	g_byte_array_append(buf, dt_struct->data, dt_struct->len);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	g_byte_array_append(buf, dt_strings->data, dt_strings->len);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_4, 0x0);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_fdt_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuFdtFirmware *self = FU_FDT_FIRMWARE(firmware);
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "cpuid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->cpuid = tmp;

	/* success */
	return TRUE;
}

static void
fu_fdt_firmware_init(FuFdtFirmware *self)
{
	g_type_ensure(FU_TYPE_FDT_IMAGE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_fdt_firmware_class_init(FuFdtFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_fdt_firmware_check_magic;
	klass_firmware->export = fu_fdt_firmware_export;
	klass_firmware->parse = fu_fdt_firmware_parse;
	klass_firmware->write = fu_fdt_firmware_write;
	klass_firmware->build = fu_fdt_firmware_build;
}

/**
 * fu_fdt_firmware_new:
 *
 * Creates a new #FuFirmware of sub type FDT
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_fdt_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FDT_FIRMWARE, NULL));
}
