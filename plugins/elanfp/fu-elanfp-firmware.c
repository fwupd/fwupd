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

#define FU_ELANTP_FIRMWARE_IDX_END 0xFF

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
fu_elanfp_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 guint64 addr_start,
			 guint64 addr_end,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	const guint8 *buf;
	gsize bufsz;
	guint32 tag = 0;
	gsize offset = 0x00;

	/* check the tag */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_common_read_uint32_safe(buf, bufsz, offset + 0x0, &tag, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (tag != 0x46325354) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "tag is not valid");
		return FALSE;
	}

	/* file format version */
	if (!fu_common_read_uint32_safe(buf,
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
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* type, reserved, start-addr, len */
		if (!fu_common_read_uint32_safe(buf,
						bufsz,
						offset + 0x0,
						&fwtype,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		fu_firmware_set_idx(img, fwtype);
		if (!fu_common_read_uint32_safe(buf,
						bufsz,
						offset + 0x8,
						&start_addr,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		fu_firmware_set_addr(img, start_addr);
		if (!fu_common_read_uint32_safe(buf,
						bufsz,
						offset + 0xC,
						&length,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		blob = fu_common_bytes_new_offset(fw, start_addr, length, error);
		if (blob == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, blob);
		fu_firmware_add_image(firmware, img);

		/* done */
		if (fwtype == FU_ELANTP_FIRMWARE_IDX_END)
			break;

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
		fu_byte_array_append_uint32(buf, fu_firmware_get_idx(img), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
		fu_byte_array_append_uint32(buf, offset, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, fu_firmware_get_size(img), G_LITTLE_ENDIAN);
		offset += fu_firmware_get_size(img);
	}

	/* end of index */
	fu_byte_array_append_uint32(buf, FU_ELANTP_FIRMWARE_IDX_END, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* assumed */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* assumed */

	/* data */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = fu_firmware_get_bytes(img, error);
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
