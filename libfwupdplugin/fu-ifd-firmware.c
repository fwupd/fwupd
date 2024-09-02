/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuIfdFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-composite-input-stream.h"
#include "fu-efi-volume.h"
#include "fu-ifd-bios.h"
#include "fu-ifd-common.h"
#include "fu-ifd-firmware.h"
#include "fu-ifd-image.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"

/**
 * FuIfdFirmware:
 *
 * An Intel Flash Descriptor.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	gboolean new_layout;
	guint32 descriptor_map0;
	guint32 descriptor_map1;
	guint32 descriptor_map2;
	guint8 num_regions;
	guint8 num_components;
	guint32 flash_region_base_addr;
	guint32 flash_component_base_addr;
	guint32 flash_master_base_addr;
	guint32 flash_master[4]; /* indexed from 1, ignore [0] */
	guint32 flash_ich_strap_base_addr;
	guint32 flash_mch_strap_base_addr;
	guint32 components_rcd;
	guint32 illegal_jedec;
	guint32 illegal_jedec1;
	guint32 *flash_descriptor_regs;
} FuIfdFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuIfdFirmware, fu_ifd_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_ifd_firmware_get_instance_private(o))

#define FU_IFD_SIZE 0x1000

#define FU_IFD_FDBAR_FLASH_UPPER_MAP1 0x0EFC
#define FU_IFD_FDBAR_OEM_SECTION      0x0F00

#define FU_IFD_FREG_BASE(freg)	(((freg) << 12) & 0x07FFF000)
#define FU_IFD_FREG_LIMIT(freg) ((((freg) >> 4) & 0x07FFF000) | 0x00000FFF)

static void
fu_ifd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE(firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "descriptor_map0", priv->descriptor_map0);
	fu_xmlb_builder_insert_kx(bn, "descriptor_map1", priv->descriptor_map1);
	fu_xmlb_builder_insert_kx(bn, "descriptor_map2", priv->descriptor_map2);
	fu_xmlb_builder_insert_kx(bn, "num_regions", priv->num_regions);
	fu_xmlb_builder_insert_kx(bn, "num_components", priv->num_components + 1);
	fu_xmlb_builder_insert_kx(bn, "flash_region_base_addr", priv->flash_region_base_addr);
	fu_xmlb_builder_insert_kx(bn, "flash_component_base_addr", priv->flash_component_base_addr);
	fu_xmlb_builder_insert_kx(bn, "flash_master_base_addr", priv->flash_master_base_addr);
	fu_xmlb_builder_insert_kx(bn, "flash_ich_strap_base_addr", priv->flash_ich_strap_base_addr);
	fu_xmlb_builder_insert_kx(bn, "flash_mch_strap_base_addr", priv->flash_mch_strap_base_addr);
	fu_xmlb_builder_insert_kx(bn, "components_rcd", priv->components_rcd);
	fu_xmlb_builder_insert_kx(bn, "illegal_jedec", priv->illegal_jedec);
	fu_xmlb_builder_insert_kx(bn, "illegal_jedec1", priv->illegal_jedec1);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		for (guint i = 1; i < 3; i++) {
			g_autofree gchar *title = g_strdup_printf("flash_master%x", i + 1);
			fu_xmlb_builder_insert_kx(bn, title, priv->flash_master[i]);
		}
		if (priv->flash_descriptor_regs != NULL) {
			for (guint i = 0; i < priv->num_regions; i++) {
				g_autofree gchar *title =
				    g_strdup_printf("flash_descriptor_reg%x", i);
				fu_xmlb_builder_insert_kx(bn,
							  title,
							  priv->flash_descriptor_regs[i]);
			}
		}
	}
}

static gboolean
fu_ifd_firmware_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_ifd_fdbar_validate_stream(stream, offset, error);
}

static GInputStream *
fu_ifd_firmware_fixup_stream(GInputStream *stream, GError **error)
{
	const guint8 buf[] = {0xFF};
	gsize streamsz = 0;
	g_autoptr(GBytes) blob = g_bytes_new(buf, sizeof(buf));
	g_autoptr(GInputStream) stream2 = fu_composite_input_stream_new();

	/* already aligned */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return NULL;
	if (((streamsz >> 1) << 1) == streamsz)
		return g_object_ref(stream);

	/* pad with one trailing byte */
	if (!fu_composite_input_stream_add_stream(FU_COMPOSITE_INPUT_STREAM(stream2),
						  stream,
						  error))
		return NULL;
	fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(stream2), blob);
	return g_steal_pointer(&stream2);
}

static gboolean
fu_ifd_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE(firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;
	g_autoptr(GByteArray) st_fcba = NULL;
	g_autoptr(GByteArray) st_fdbar = NULL;
	g_autoptr(GInputStream) stream2 = NULL;

	/* check size */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < FU_IFD_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "file is too small, expected streamsz >= 0x%x",
			    (guint)FU_IFD_SIZE);
		return FALSE;
	}

	/* some test IFD images were captured missing the final byte -- so align up */
	stream2 = fu_ifd_firmware_fixup_stream(stream, error);
	if (stream2 == NULL)
		return FALSE;

	/* descriptor registers */
	st_fdbar = fu_struct_ifd_fdbar_parse_stream(stream, 0x0, error);
	if (st_fdbar == NULL)
		return FALSE;
	priv->descriptor_map0 = fu_struct_ifd_fdbar_get_descriptor_map0(st_fdbar);
	priv->num_regions = (priv->descriptor_map0 >> 24) & 0b111;
	if (priv->num_regions == 0)
		priv->num_regions = 10;
	priv->num_components = (priv->descriptor_map0 >> 8) & 0b11;
	priv->flash_component_base_addr = (priv->descriptor_map0 << 4) & 0x00000FF0;
	priv->flash_region_base_addr = (priv->descriptor_map0 >> 12) & 0x00000FF0;
	priv->descriptor_map1 = fu_struct_ifd_fdbar_get_descriptor_map1(st_fdbar);
	priv->flash_master_base_addr = (priv->descriptor_map1 << 4) & 0x00000FF0;
	priv->flash_ich_strap_base_addr = (priv->descriptor_map1 >> 12) & 0x00000FF0;
	priv->descriptor_map2 = fu_struct_ifd_fdbar_get_descriptor_map2(st_fdbar);
	priv->flash_mch_strap_base_addr = (priv->descriptor_map2 << 4) & 0x00000FF0;

	/* FCBA */
	st_fcba = fu_struct_ifd_fcba_parse_stream(stream, priv->flash_component_base_addr, error);
	if (st_fcba == NULL)
		return FALSE;
	priv->components_rcd = fu_struct_ifd_fcba_get_flcomp(st_fcba);
	priv->illegal_jedec = fu_struct_ifd_fcba_get_flill(st_fcba);
	priv->illegal_jedec1 = fu_struct_ifd_fcba_get_flill1(st_fcba);

	/* FMBA */
	if (!fu_input_stream_read_u32(stream,
				      priv->flash_master_base_addr + 0x0,
				      &priv->flash_master[1],
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u32(stream,
				      priv->flash_master_base_addr + 0x4,
				      &priv->flash_master[2],
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u32(stream,
				      priv->flash_master_base_addr + 0x8,
				      &priv->flash_master[3],
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	/* FRBA */
	priv->flash_descriptor_regs = g_new0(guint32, priv->num_regions);
	for (guint i = 0; i < priv->num_regions; i++) {
		if (!fu_input_stream_read_u32(stream,
					      priv->flash_region_base_addr + (i * sizeof(guint32)),
					      &priv->flash_descriptor_regs[i],
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;
	}
	for (guint i = 0; i < priv->num_regions; i++) {
		const gchar *freg_str = fu_ifd_region_to_string(i);
		guint32 freg_base = FU_IFD_FREG_BASE(priv->flash_descriptor_regs[i]);
		guint32 freg_limt = FU_IFD_FREG_LIMIT(priv->flash_descriptor_regs[i]);
		guint32 freg_size = (freg_limt - freg_base) + 1;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GInputStream) partial_stream = NULL;

		/* invalid */
		if (freg_base > freg_limt)
			continue;

		/* create image */
		g_debug("freg %s 0x%04x -> 0x%04x", freg_str, freg_base, freg_limt);
		partial_stream = fu_partial_input_stream_new(stream2, freg_base, freg_size, error);
		if (partial_stream == NULL)
			return FALSE;
		if (i == FU_IFD_REGION_BIOS) {
			img = fu_ifd_bios_new();
		} else {
			img = fu_ifd_image_new();
		}
		if (!fu_firmware_parse_stream(img,
					      partial_stream,
					      0x0,
					      flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
					      error))
			return FALSE;
		fu_firmware_set_addr(img, freg_base);
		fu_firmware_set_idx(img, i);
		if (freg_str != NULL)
			fu_firmware_set_id(img, freg_str);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* is writable by anything other than the region itself */
		for (FuIfdRegion r = 1; r <= 3; r++) {
			FuIfdAccess acc;
			acc = fu_ifd_region_to_access(i, priv->flash_master[r], priv->new_layout);
			fu_ifd_image_set_access(FU_IFD_IMAGE(img), r, acc);
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_ifd_firmware_check_jedec_cmd:
 * @self: a #FuIfdFirmware
 * @cmd: a JEDEC command, e.g. 0x42 for "whole chip erase"
 *
 * Checks a JEDEC command to see if it has been put on the "illegal_jedec" list.
 *
 * Returns: %TRUE if the command is allowed
 *
 * Since: 1.6.2
 **/
gboolean
fu_ifd_firmware_check_jedec_cmd(FuIfdFirmware *self, guint8 cmd)
{
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);
	for (guint j = 0; j < 32; j += 8) {
		if (((priv->illegal_jedec >> j) & 0xff) == cmd)
			return FALSE;
		if (((priv->illegal_jedec1 >> j) & 0xff) == cmd)
			return FALSE;
	}
	return TRUE;
}

static GByteArray *
fu_ifd_firmware_write(FuFirmware *firmware, GError **error)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE(firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize bufsz_max = 0x0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_fcba = fu_struct_ifd_fcba_new();
	g_autoptr(GByteArray) st_fdbar = fu_struct_ifd_fdbar_new();
	g_autoptr(GHashTable) blobs = NULL;
	g_autoptr(FuFirmware) img_desc = NULL;

	/* if the descriptor does not exist, then add something plausible */
	img_desc = fu_firmware_get_image_by_idx(firmware, FU_IFD_REGION_DESC, NULL);
	if (img_desc == NULL) {
		g_autoptr(GByteArray) buf_desc = g_byte_array_new();
		g_autoptr(GBytes) blob_desc = NULL;
		fu_byte_array_set_size(buf_desc, FU_IFD_SIZE, 0x00);

		/* success */
		blob_desc = g_bytes_new(buf_desc->data, buf_desc->len);
		img_desc = fu_firmware_new_from_bytes(blob_desc);
		fu_firmware_set_addr(img_desc, 0x0);
		fu_firmware_set_idx(img_desc, FU_IFD_REGION_DESC);
		fu_firmware_set_id(img_desc, "desc");
		fu_firmware_add_image(firmware, img_desc);
	}

	/* generate ahead of time */
	blobs = g_hash_table_new_full(g_direct_hash,
				      g_direct_equal,
				      NULL,
				      (GDestroyNotify)g_bytes_unref);
	for (guint i = 0; i < priv->num_regions; i++) {
		g_autoptr(FuFirmware) img = fu_firmware_get_image_by_idx(firmware, i, NULL);
		g_autoptr(GBytes) blob = NULL;

		if (img == NULL)
			continue;
		blob = fu_firmware_write(img, error);
		if (blob == NULL) {
			g_prefix_error(error, "failed to write %s: ", fu_firmware_get_id(img));
			return NULL;
		}
		if (g_bytes_get_data(blob, NULL) == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to write %s",
				    fu_firmware_get_id(img));
			return NULL;
		}
		g_hash_table_insert(blobs, GUINT_TO_POINTER(i), g_bytes_ref(blob));

		/* check total size */
		bufsz_max = MAX(fu_firmware_get_addr(img) + g_bytes_get_size(blob), bufsz_max);
	}
	fu_byte_array_set_size(buf, bufsz_max, 0x00);

	/* descriptor map */
	fu_struct_ifd_fdbar_set_descriptor_map0(st_fdbar, priv->descriptor_map0);
	fu_struct_ifd_fdbar_set_descriptor_map1(st_fdbar, priv->descriptor_map1);
	fu_struct_ifd_fdbar_set_descriptor_map2(st_fdbar, priv->descriptor_map2);
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    0x0,
			    st_fdbar->data,
			    st_fdbar->len,
			    0x0,
			    st_fdbar->len,
			    error))
		return NULL;

	/* FCBA */
	fu_struct_ifd_fcba_set_flcomp(st_fcba, priv->components_rcd);
	fu_struct_ifd_fcba_set_flill(st_fcba, priv->illegal_jedec);
	fu_struct_ifd_fcba_set_flill1(st_fcba, priv->illegal_jedec1);
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    priv->flash_component_base_addr,
			    st_fcba->data,
			    st_fcba->len,
			    0x0,
			    st_fcba->len,
			    error))
		return NULL;

	/* FRBA */
	for (guint i = 0; i < priv->num_regions; i++) {
		guint32 freg_base = 0x7FFF000;
		guint32 freg_limt = 0x0;
		guint32 flreg;
		g_autoptr(FuFirmware) img = fu_firmware_get_image_by_idx(firmware, i, NULL);
		if (img != NULL) {
			GBytes *blob =
			    g_hash_table_lookup(blobs, GUINT_TO_POINTER(fu_firmware_get_idx(img)));
			freg_base = fu_firmware_get_addr(img);
			freg_limt = (freg_base + g_bytes_get_size(blob)) - 1;
		}
		flreg = ((freg_limt << 4) & 0xFFFF0000) | (freg_base >> 12);
		g_debug("freg 0x%04x -> 0x%04x = 0x%08x", freg_base, freg_limt, flreg);
		if (!fu_memwrite_uint32_safe(buf->data,
					     buf->len,
					     priv->flash_region_base_addr + (i * sizeof(guint32)),
					     flreg,
					     G_LITTLE_ENDIAN,
					     error))
			return NULL;
	}

	/* write images at correct offsets */
	for (guint i = 1; i < priv->num_regions; i++) {
		GBytes *blob;
		g_autoptr(FuFirmware) img = fu_firmware_get_image_by_idx(firmware, i, NULL);
		if (img == NULL)
			continue;
		blob = g_hash_table_lookup(blobs, GUINT_TO_POINTER(fu_firmware_get_idx(img)));
		if (!fu_memcpy_safe(buf->data,
				    buf->len,
				    fu_firmware_get_addr(img),
				    g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    0x0,
				    g_bytes_get_size(blob),
				    error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_ifd_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE(firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "descriptor_map0", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->descriptor_map0 = tmp;
	tmp = xb_node_query_text_as_uint(n, "descriptor_map1", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->descriptor_map1 = tmp;
	tmp = xb_node_query_text_as_uint(n, "descriptor_map2", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->descriptor_map2 = tmp;
	tmp = xb_node_query_text_as_uint(n, "components_rcd", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->components_rcd = tmp;
	tmp = xb_node_query_text_as_uint(n, "illegal_jedec", NULL);
	if (tmp != G_MAXUINT64) {
		priv->illegal_jedec = tmp & 0xFFFFFFFF;
		priv->illegal_jedec1 = tmp >> 32;
	}

	/* success */
	return TRUE;
}

static void
fu_ifd_firmware_init(FuIfdFirmware *self)
{
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);

	/* some good defaults */
	priv->new_layout = TRUE;
	priv->num_regions = 10;
	priv->flash_region_base_addr = 0x40;
	priv->flash_component_base_addr = 0x30;
	priv->flash_master_base_addr = 0x80;
	priv->flash_master[1] = 0x00A00F00;
	priv->flash_master[2] = 0x00400D00;
	priv->flash_master[3] = 0x00800900;
	priv->flash_ich_strap_base_addr = 0x100;
	priv->flash_mch_strap_base_addr = 0x300;
	g_type_ensure(FU_TYPE_IFD_BIOS);
	g_type_ensure(FU_TYPE_IFD_IMAGE);
	g_type_ensure(FU_TYPE_EFI_VOLUME);
}

static void
fu_ifd_firmware_finalize(GObject *object)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE(object);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE(self);
	g_free(priv->flash_descriptor_regs);
	G_OBJECT_CLASS(fu_ifd_firmware_parent_class)->finalize(object);
}

static void
fu_ifd_firmware_class_init(FuIfdFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_ifd_firmware_finalize;
	firmware_class->validate = fu_ifd_firmware_validate;
	firmware_class->export = fu_ifd_firmware_export;
	firmware_class->parse = fu_ifd_firmware_parse;
	firmware_class->write = fu_ifd_firmware_write;
	firmware_class->build = fu_ifd_firmware_build;
}

/**
 * fu_ifd_firmware_new:
 *
 * Creates a new #FuFirmware of sub type Ifd
 *
 * Since: 1.6.2
 **/
FuFirmware *
fu_ifd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IFD_FIRMWARE, NULL));
}
