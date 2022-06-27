/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

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
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "unable to calculate the checksum of the update binary");
		return NULL;
	}
	return g_strdup_printf("%x", priv->crc32);
}

static guint32
fu_nordic_hid_firmware_crc32(const guint8 *buf, gsize bufsz)
{
	guint crc32 = 0x01;
	/* maybe skipped "^" step in fu_crc32_full()?
	 * according https://github.com/madler/zlib/blob/master/crc32.c#L225 */
	crc32 ^= 0xFFFFFFFFUL;
	return fu_crc32_full(buf, bufsz, crc32, 0xEDB88320);
}

static gboolean
fu_nordic_hid_firmware_parse(FuFirmware *firmware,
			     GBytes *fw,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuNordicHidFirmware *self = FU_NORDIC_HID_FIRMWARE(firmware);
	FuNordicHidFirmwarePrivate *priv = GET_PRIVATE(self);
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
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	priv->crc32 = fu_nordic_hid_firmware_crc32(buf, bufsz);

	/* do not strip the header */
	fu_firmware_set_bytes(firmware, fw);

	return TRUE;
}

static void
fu_nordic_hid_firmware_init(FuNordicHidFirmware *self)
{
}

static void
fu_nordic_hid_firmware_class_init(FuNordicHidFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);

	klass_firmware->export = fu_nordic_hid_firmware_export;
	klass_firmware->get_checksum = fu_nordic_hid_firmware_get_checksum;
	klass_firmware->parse = fu_nordic_hid_firmware_parse;
}
