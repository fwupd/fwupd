/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-elanfp-firmware.h"

struct _FuElanfpFirmware {
	FuFirmwareClass parent_instance;
	guint32 format_version;
};

G_DEFINE_TYPE(FuElanfpFirmware, fu_elanfp_firmware, FU_TYPE_FIRMWARE)

#define FU_ELANFP_FIRMWARE_HEADER_MAGIC 0x46325354

static void
fu_elanfp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "format_version", self->format_version);
}

static gboolean
fu_elanfp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "format_version", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->format_version = tmp;

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint32 magic = 0;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != FU_ELANFP_FIRMWARE_HEADER_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid magic, expected 0x%04X got 0x%04X",
			    (guint32)FU_ELANFP_FIRMWARE_HEADER_MAGIC,
			    magic);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	const guint8 *buf;
	gsize bufsz;
	guint img_cnt = 0;

	/* file format version */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + 0x4,
				    &self->format_version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* read indexes */
	offset += 0x10;
	while (1) {
		guint32 start_addr = 0;
		guint32 length = 0;
		guint32 fwtype = 0;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(FuFirmware) img = NULL;

		/* check sanity */
		if (img_cnt++ > 256) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "too many images detected");
			return FALSE;
		}

		/* type, reserved, start-addr, len */
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + 0x0,
					    &fwtype,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		/* check not already added */
		img = fu_firmware_get_image_by_idx(firmware, fwtype, NULL);
		if (img != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "already parsed image with fwtype 0x%x",
				    fwtype);
			return FALSE;
		}

		/* done */
		if (fwtype == FU_ELANTP_FIRMWARE_IDX_END)
			break;
		switch (fwtype) {
		case FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_A:
		case FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_B:
			img = fu_cfu_offer_new();
			break;
		case FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_A:
		case FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_B:
			img = fu_cfu_payload_new();
			break;
		default:
			img = fu_firmware_new();
			break;
		}
		fu_firmware_set_idx(img, fwtype);
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + 0x8,
					    &start_addr,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		fu_firmware_set_addr(img, start_addr);
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + 0xC,
					    &length,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (length == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "zero size fwtype 0x%x not supported",
				    fwtype);
			return FALSE;
		}
		blob = fu_bytes_new_offset(fw, start_addr, length, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_firmware_parse(img, blob, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error))
			return FALSE;
		fu_firmware_add_image(firmware, img);

		offset += 0x10;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_elanfp_firmware_write(FuFirmware *firmware, GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	gsize offset = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* S2F_HEADER */
	fu_byte_array_append_uint32(buf, 0x46325354, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, self->format_version, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* ICID, assumed */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */

	/* S2F_INDEX */
	offset += 0x10 + ((imgs->len + 1) * 0x10);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_uint32(buf, fu_firmware_get_idx(img), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
		fu_byte_array_append_uint32(buf, offset, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, g_bytes_get_size(blob), G_LITTLE_ENDIAN);
		offset += g_bytes_get_size(blob);
	}

	/* end of index */
	fu_byte_array_append_uint32(buf, FU_ELANTP_FIRMWARE_IDX_END, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* assumed */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* assumed */

	/* data */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_elanfp_firmware_init(FuElanfpFirmware *self)
{
}

static void
fu_elanfp_firmware_class_init(FuElanfpFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_elanfp_firmware_check_magic;
	klass_firmware->parse = fu_elanfp_firmware_parse;
	klass_firmware->write = fu_elanfp_firmware_write;
	klass_firmware->export = fu_elanfp_firmware_export;
	klass_firmware->build = fu_elanfp_firmware_build;
}

FuFirmware *
fu_elanfp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELANFP_FIRMWARE, NULL));
}
