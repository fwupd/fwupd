/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-algoltek-usbcr-firmware.h"
#include "fu-algoltek-usbcr-struct.h"

struct _FuAlgoltekUsbcrFirmware {
	FuFirmware parent_instance;
	guint16 boot_ver;
};

G_DEFINE_TYPE(FuAlgoltekUsbcrFirmware, fu_algoltek_usbcr_firmware, FU_TYPE_FIRMWARE)

static void
fu_algoltek_usbcr_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuAlgoltekUsbcrFirmware *self = FU_ALGOLTEK_USBCR_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "boot_ver", self->boot_ver);
}

static gboolean
fu_algoltek_usbcr_firmware_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	FuAlgoltekUsbcrFirmware *self = FU_ALGOLTEK_USBCR_FIRMWARE(firmware);
	gsize offset = 0;
	guint16 app_ver = 0;
	guint16 emmc_support_ver = 0;
	guint16 emmc_ver = 0;
	guint16 fw_addr = 0;
	guint16 fw_len = 0;

	/* emmc version */
	if (!fu_input_stream_read_u16(stream,
				      FU_AG_USBCR_OFFSET_EMMC_VER,
				      &emmc_ver,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u16(stream,
				      FU_AG_USBCR_OFFSET_FIRMWARE_START_ADDR,
				      &fw_addr,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u16(stream,
				      FU_AG_USBCR_OFFSET_FIRMWARE_LEN,
				      &fw_len,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;

	/* calculate the offset of the app_ver */
	offset += fw_addr + fw_len - FU_AG_USBCR_OFFSET_APP_VER_FROM_END;

	/* app version */
	if (!fu_input_stream_read_u16(stream, offset, &app_ver, G_BIG_ENDIAN, error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, app_ver);

	/* boot version */
	offset += 2;
	if (!fu_input_stream_read_u16(stream, offset, &self->boot_ver, G_BIG_ENDIAN, error))
		return FALSE;

	/* emmc support version */
	offset += FU_AG_USBCR_OFFSET_EMMC_SUPPORT_VER_FROM_BOOT_VER;
	if (!fu_input_stream_read_u16(stream, offset, &emmc_support_ver, G_BIG_ENDIAN, error))
		return FALSE;
	if (emmc_ver != emmc_support_ver) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "EMMC support version is 0x%x while expecting value is 0x%x",
			    emmc_support_ver,
			    emmc_ver);
		return FALSE;
	}

	return TRUE;
}

guint16
fu_algoltek_usbcr_firmware_get_boot_ver(FuAlgoltekUsbcrFirmware *self)
{
	g_return_val_if_fail(FU_IS_ALGOLTEK_USBCR_FIRMWARE(self), G_MAXUINT16);
	return self->boot_ver;
}

static gchar *
fu_algoltek_usbcr_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint16_hex(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_algoltek_usbcr_firmware_init(FuAlgoltekUsbcrFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
}

static void
fu_algoltek_usbcr_firmware_class_init(FuAlgoltekUsbcrFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_algoltek_usbcr_firmware_convert_version;
	firmware_class->parse = fu_algoltek_usbcr_firmware_parse;
	firmware_class->export = fu_algoltek_usbcr_firmware_export;
}

FuFirmware *
fu_algoltek_usbcr_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ALGOLTEK_USBCR_FIRMWARE, NULL));
}
