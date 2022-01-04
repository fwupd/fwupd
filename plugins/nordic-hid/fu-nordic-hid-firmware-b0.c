/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-nordic-hid-firmware-b0.h"

#define UPDATE_IMAGE_MAGIC_COMMON 0x281ee6de
#define UPDATE_IMAGE_MAGIC_FWINFO 0x8fcebb4c
#define UPDATE_IMAGE_MAGIC_NRF52  0x00003402
#define UPDATE_IMAGE_MAGIC_NRF53  0x00003502

struct _FuNordicHidFirmwareB0 {
	FuIhexFirmwareClass parent_instance;
	guint32 crc32;
};

G_DEFINE_TYPE(FuNordicHidFirmwareB0, fu_nordic_hid_firmware_b0, FU_TYPE_FIRMWARE)

static void
fu_nordic_hid_firmware_b0_export(FuFirmware *firmware,
				 FuFirmwareExportFlags flags,
				 XbBuilderNode *bn)
{
	FuNordicHidFirmwareB0 *self = FU_NORDIC_HID_FIRMWARE_B0(firmware);
	fu_xmlb_builder_insert_kx(bn, "crc32", self->crc32);
}

static GBytes *
fu_nordic_hid_firmware_b0_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	fu_byte_array_append_uint32(buf, UPDATE_IMAGE_MAGIC_COMMON, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, UPDATE_IMAGE_MAGIC_FWINFO, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, UPDATE_IMAGE_MAGIC_NRF52, G_LITTLE_ENDIAN);
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_nordic_hid_firmware_b0_read_fwinfo(guint8 const *buf, gsize bufsz, GError **error)
{
	guint32 magic_common;
	guint32 magic_fwinfo;
	guint32 magic_compat;
	guint32 offset;
	guint32 hdr_offset[5] = {0x0000, 0x0200, 0x400, 0x800, 0x1000};

	/* find correct offset to fwinfo */
	for (guint32 i = 0; i < G_N_ELEMENTS(hdr_offset); i++) {
		offset = hdr_offset[i];
		if (!fu_common_read_uint32_safe(buf,
						bufsz,
						offset,
						&magic_common,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		if (!fu_common_read_uint32_safe(buf,
						bufsz,
						offset + 0x04,
						&magic_fwinfo,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		if (!fu_common_read_uint32_safe(buf,
						bufsz,
						offset + 0x08,
						&magic_compat,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		if (magic_common != UPDATE_IMAGE_MAGIC_COMMON ||
		    magic_fwinfo != UPDATE_IMAGE_MAGIC_FWINFO)
			continue;
		switch (magic_compat) {
		case UPDATE_IMAGE_MAGIC_NRF52:
		case UPDATE_IMAGE_MAGIC_NRF53:
			return TRUE;
		default:
			break;
		}
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unable to validate the update binary");
	return FALSE;
}

static guint32
fu_nordic_hid_firmware_b0_crc32(const guint8 *buf, gsize bufsz)
{
	guint crc32 = 0x01;
	/* maybe skipped "^" step in fu_common_crc32_full()?
	 * according https://github.com/madler/zlib/blob/master/crc32.c#L225 */
	crc32 ^= 0xFFFFFFFFUL;
	return fu_common_crc32_full(buf, bufsz, crc32, 0xEDB88320);
}

static gchar *
fu_nordic_hid_firmware_b0_get_checksum(FuFirmware *firmware,
				       GChecksumType csum_kind,
				       GError **error)
{
	FuNordicHidFirmwareB0 *self = FU_NORDIC_HID_FIRMWARE_B0(firmware);
	if (!fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECKSUM)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "unable to calculate the checksum of the update binary");
		return NULL;
	}
	return g_strdup_printf("%x", self->crc32);
}

static gboolean
fu_nordic_hid_firmware_b0_parse(FuFirmware *firmware,
				GBytes *fw,
				guint64 addr_start,
				guint64 addr_end,
				FwupdInstallFlags flags,
				GError **error)
{
	FuNordicHidFirmwareB0 *self = FU_NORDIC_HID_FIRMWARE_B0(firmware);
	const guint8 *buf;
	gsize bufsz = 0;

	buf = g_bytes_get_data(fw, &bufsz);
	if (buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unable to get the image binary");
		return FALSE;
	}
	if (!fu_nordic_hid_firmware_b0_read_fwinfo(buf, bufsz, error))
		return FALSE;
	self->crc32 = fu_nordic_hid_firmware_b0_crc32(buf, bufsz);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);

	/* do not strip the header */
	fu_firmware_set_bytes(firmware, fw);

	/* success */
	return TRUE;
}

static void
fu_nordic_hid_firmware_b0_init(FuNordicHidFirmwareB0 *self)
{
}

static void
fu_nordic_hid_firmware_b0_class_init(FuNordicHidFirmwareB0Class *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->get_checksum = fu_nordic_hid_firmware_b0_get_checksum;
	klass_firmware->export = fu_nordic_hid_firmware_b0_export;
	klass_firmware->parse = fu_nordic_hid_firmware_b0_parse;
	klass_firmware->write = fu_nordic_hid_firmware_b0_write;
}

FuFirmware *
fu_nordic_hid_firmware_b0_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_B0, NULL));
}
