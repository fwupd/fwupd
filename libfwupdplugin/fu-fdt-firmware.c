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
#include "fu-fdt-struct.h"
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

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP	       0x00000004
#define FDT_END	       0x00000009

#define FDT_LAST_COMP_VERSION 2
#define FDT_DEPTH_MAX	      128

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
fu_fdt_firmware_parse_dt_struct(FuFdtFirmware *self, GBytes *fw, GBytes *strtab, GError **error)
{
	gsize bufsz = 0;
	gsize offset = 0;
	guint depth = 0;
	gboolean has_end = FALSE;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(FuFirmware) firmware_current = g_object_ref(FU_FIRMWARE(self));

	/* debug */
	fu_dump_bytes(G_LOG_DOMAIN, "dt_struct", fw);

	/* parse */
	while (offset < bufsz) {
		guint32 token = 0;

		/* read tag from aligned offset */
		offset = fu_common_align_up(offset, FU_FIRMWARE_ALIGNMENT_4);
		if (!fu_memread_uint32_safe(buf, bufsz, offset, &token, G_BIG_ENDIAN, error))
			return FALSE;
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

			/* sanity check */
			if (depth++ > FDT_DEPTH_MAX) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "node depth exceeded maximum: 0x%x",
					    (guint)FDT_DEPTH_MAX);
				return FALSE;
			}

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
			if (depth > 0)
				depth--;
			continue;
		}

		/* PROP */
		if (token == FDT_PROP) {
			guint32 prop_len;
			guint32 prop_nameoff;
			g_autoptr(GBytes) blob = NULL;
			g_autoptr(GString) str = NULL;
			g_autoptr(GByteArray) st_prp = NULL;

			/* sanity check */
			if (firmware_current == FU_FIRMWARE(self)) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "got PROP with unopen node");
				return FALSE;
			}

			/* parse */
			st_prp = fu_struct_fdt_prop_parse(buf, bufsz, offset, error);
			if (st_prp == NULL)
				return FALSE;
			prop_len = fu_struct_fdt_prop_get_len(st_prp);
			prop_nameoff = fu_struct_fdt_prop_get_nameoff(st_prp);
			offset += st_prp->len;

			/* add property */
			str = fu_string_new_safe(g_bytes_get_data(strtab, NULL),
						 g_bytes_get_size(strtab),
						 prop_nameoff,
						 error);
			if (str == NULL) {
				g_prefix_error(error, "invalid strtab offset 0x%x: ", prop_nameoff);
				return FALSE;
			}
			blob = fu_bytes_new_offset(fw, offset, prop_len, error);
			if (blob == NULL)
				return FALSE;
			fu_fdt_image_set_attr(FU_FDT_IMAGE(firmware_current), str->str, blob);
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
fu_fdt_firmware_parse_mem_rsvmap(FuFdtFirmware *self,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 GError **error)
{
	/* parse */
	for (; offset < bufsz; offset += FU_STRUCT_FDT_RESERVE_ENTRY_SIZE) {
		guint64 address = 0;
		guint64 size = 0;
		g_autoptr(GByteArray) st_res = NULL;
		st_res = fu_struct_fdt_reserve_entry_parse(buf, bufsz, offset, error);
		if (st_res == NULL)
			return FALSE;
		address = fu_struct_fdt_reserve_entry_get_address(st_res);
		size = fu_struct_fdt_reserve_entry_get_size(st_res);
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
	return fu_struct_fdt_validate(g_bytes_get_data(fw, NULL),
				      g_bytes_get_size(fw),
				      offset,
				      error);
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
	guint32 totalsize;
	gsize bufsz = 0;
	guint32 off_mem_rsvmap = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* sanity check */
	st_hdr = fu_struct_fdt_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	totalsize = fu_struct_fdt_get_totalsize(st_hdr);
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

	/* read header */
	priv->cpuid = fu_struct_fdt_get_boot_cpuid_phys(st_hdr);
	off_mem_rsvmap = fu_struct_fdt_get_off_mem_rsvmap(st_hdr);
	if (off_mem_rsvmap != 0x0) {
		if (!fu_fdt_firmware_parse_mem_rsvmap(self,
						      buf,
						      bufsz,
						      offset + off_mem_rsvmap,
						      error))
			return FALSE;
	}
	if (fu_struct_fdt_get_last_comp_version(st_hdr) < FDT_LAST_COMP_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid header version, got 0x%x, expected >= 0x%x",
			    (guint)fu_struct_fdt_get_last_comp_version(st_hdr),
			    (guint)FDT_LAST_COMP_VERSION);
		return FALSE;
	}
	fu_firmware_set_version_raw(firmware, fu_struct_fdt_get_version(st_hdr));

	/* parse device tree struct */
	if (fu_struct_fdt_get_size_dt_struct(st_hdr) != 0x0 &&
	    fu_struct_fdt_get_size_dt_strings(st_hdr) != 0x0) {
		g_autoptr(GBytes) dt_strings = NULL;
		g_autoptr(GBytes) dt_struct = NULL;
		dt_strings = fu_bytes_new_offset(fw,
						 offset + fu_struct_fdt_get_off_dt_strings(st_hdr),
						 fu_struct_fdt_get_size_dt_strings(st_hdr),
						 error);
		if (dt_strings == NULL)
			return FALSE;
		dt_struct = fu_bytes_new_offset(fw,
						offset + fu_struct_fdt_get_off_dt_struct(st_hdr),
						fu_struct_fdt_get_size_dt_struct(st_hdr),
						error);
		if (dt_struct == NULL)
			return FALSE;
		if (!fu_fdt_firmware_parse_dt_struct(self, dt_struct, dt_strings, error))
			return FALSE;
	}

	/* success */
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

	g_debug("adding strtab: %s", key);
	offset = helper->dt_strings->len;
	g_byte_array_append(helper->dt_strings, (const guint8 *)key, strlen(key));
	fu_byte_array_append_uint8(helper->dt_strings, 0x0);
	g_hash_table_insert(helper->strtab, g_strdup(key), GUINT_TO_POINTER(offset));
	return offset;
}

static gboolean
fu_fdt_firmware_write_image(FuFdtFirmware *self,
			    FuFdtImage *img,
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
		g_autoptr(GByteArray) st_prp = fu_struct_fdt_prop_new();

		blob = fu_fdt_image_get_attr(img, key, error);
		if (blob == NULL)
			return FALSE;
		fu_byte_array_append_uint32(helper->dt_struct, FDT_PROP, G_BIG_ENDIAN);
		fu_struct_fdt_prop_set_len(st_prp, g_bytes_get_size(blob));
		fu_struct_fdt_prop_set_nameoff(st_prp,
					       fu_fdt_firmware_append_to_strtab(helper, key));
		g_byte_array_append(helper->dt_struct, st_prp->data, st_prp->len);
		fu_byte_array_append_bytes(helper->dt_struct, blob);
		fu_byte_array_align_up(helper->dt_struct, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	}

	/* write children, recursively */
	for (guint i = 0; i < images->len; i++) {
		FuFdtImage *img_child = g_ptr_array_index(images, i);
		if (!fu_fdt_firmware_write_image(self, img_child, helper, depth + 1, error))
			return FALSE;
	}

	/* END_NODE */
	fu_byte_array_append_uint32(helper->dt_struct, FDT_END_NODE, G_BIG_ENDIAN);
	return TRUE;
}

static GByteArray *
fu_fdt_firmware_write(FuFirmware *firmware, GError **error)
{
	FuFdtFirmware *self = FU_FDT_FIRMWARE(firmware);
	FuFdtFirmwarePrivate *priv = GET_PRIVATE(self);
	guint32 off_dt_struct;
	guint32 off_dt_strings;
	guint32 off_mem_rsvmap;
	g_autoptr(GByteArray) dt_strings = g_byte_array_new();
	g_autoptr(GByteArray) dt_struct = g_byte_array_new();
	g_autoptr(GHashTable) strtab = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) st_hdr = fu_struct_fdt_new();
	g_autoptr(GByteArray) mem_rsvmap = fu_struct_fdt_reserve_entry_new();
	FuFdtFirmwareBuildHelper helper = {
	    .dt_strings = dt_strings,
	    .dt_struct = dt_struct,
	    .strtab = strtab,
	};

	/* empty mem_rsvmap */
	off_mem_rsvmap = fu_common_align_up(st_hdr->len, FU_FIRMWARE_ALIGNMENT_4);

	/* dt_struct */
	off_dt_struct =
	    fu_common_align_up(off_mem_rsvmap + mem_rsvmap->len, FU_FIRMWARE_ALIGNMENT_4);

	/* only one root node supported */
	if (images->len != 1) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no root node");
		return NULL;
	}
	if (!fu_fdt_firmware_write_image(self,
					 FU_FDT_IMAGE(g_ptr_array_index(images, 0)),
					 &helper,
					 0,
					 error))
		return NULL;
	fu_byte_array_append_uint32(dt_struct, FDT_END, G_BIG_ENDIAN);

	/* dt_strings */
	off_dt_strings =
	    fu_common_align_up(off_dt_struct + dt_struct->len, FU_FIRMWARE_ALIGNMENT_4);

	/* write header */
	fu_struct_fdt_set_totalsize(st_hdr, off_dt_strings + dt_strings->len);
	fu_struct_fdt_set_off_dt_struct(st_hdr, off_dt_struct);
	fu_struct_fdt_set_off_dt_strings(st_hdr, off_dt_strings);
	fu_struct_fdt_set_off_mem_rsvmap(st_hdr, off_mem_rsvmap);
	fu_struct_fdt_set_version(st_hdr, fu_firmware_get_version_raw(firmware));
	fu_struct_fdt_set_boot_cpuid_phys(st_hdr, priv->cpuid);
	fu_struct_fdt_set_size_dt_strings(st_hdr, dt_strings->len);
	fu_struct_fdt_set_size_dt_struct(st_hdr, dt_struct->len);
	fu_byte_array_align_up(st_hdr, FU_FIRMWARE_ALIGNMENT_4, 0x0);

	/* write mem_rsvmap, dt_struct, dt_strings */
	g_byte_array_append(st_hdr, mem_rsvmap->data, mem_rsvmap->len);
	fu_byte_array_align_up(st_hdr, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	g_byte_array_append(st_hdr, dt_struct->data, dt_struct->len);
	fu_byte_array_align_up(st_hdr, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	g_byte_array_append(st_hdr, dt_strings->data, dt_strings->len);
	fu_byte_array_align_up(st_hdr, FU_FIRMWARE_ALIGNMENT_4, 0x0);

	/* success */
	return g_steal_pointer(&st_hdr);
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
