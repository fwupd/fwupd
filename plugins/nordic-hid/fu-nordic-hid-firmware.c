/*
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nordic-hid-firmware.h"

typedef struct {
	guint32 crc32;
} FuNordicHidFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuNordicHidFirmware, fu_nordic_hid_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_nordic_hid_firmware_get_instance_private(o))

static void
fu_nordic_hid_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuNordicHidFirmware *self = FU_NORDIC_HID_FIRMWARE(firmware);
	FuNordicHidFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "crc32", priv->crc32);
}

static gchar *
fu_nordic_hid_firmware_get_checksum(FuFirmware *firmware, GChecksumType csum_kind, GError **error)
{
	FuNordicHidFirmware *self = FU_NORDIC_HID_FIRMWARE(firmware);
	FuNordicHidFirmwarePrivate *priv = GET_PRIVATE(self);
	if (!fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECKSUM)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unable to calculate the checksum of the update binary");
		return NULL;
	}
	return g_strdup_printf("%x", priv->crc32);
}

static gboolean
fu_nordic_hid_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuNordicHidFirmware *self = FU_NORDIC_HID_FIRMWARE(firmware);
	FuNordicHidFirmwarePrivate *priv = GET_PRIVATE(self);

	priv->crc32 = 0x01;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &priv->crc32, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_nordic_hid_firmware_init(FuNordicHidFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_nordic_hid_firmware_class_init(FuNordicHidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	firmware_class->export = fu_nordic_hid_firmware_export;
	firmware_class->get_checksum = fu_nordic_hid_firmware_get_checksum;
	firmware_class->parse = fu_nordic_hid_firmware_parse;
}
