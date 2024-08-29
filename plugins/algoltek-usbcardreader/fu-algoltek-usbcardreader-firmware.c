/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-algoltek-usbcardreader-firmware.h"
#include "fu-algoltek-usbcardreader-struct.h"

struct _FuAlgoltekUsbcardreaderFirmware {
	FuFirmware parent_instance;
	guint16 app_ver;
	guint16 boot_ver;
};

G_DEFINE_TYPE(FuAlgoltekUsbcardreaderFirmware, fu_algoltek_usbcardreader_firmware, FU_TYPE_FIRMWARE)

static void
fu_algoltek_usbcardreader_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuAlgoltekUsbcardreaderFirmware *self = FU_ALGOLTEK_USBCARDREADER_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "app_ver", self->app_ver);
	fu_xmlb_builder_insert_kx(bn, "boot_ver", self->boot_ver);
}

static gboolean
fu_algoltek_usbcardreader_firmware_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuAlgoltekUsbcardreaderFirmware *self = FU_ALGOLTEK_USBCARDREADER_FIRMWARE(firmware);

	g_autofree gchar *version = NULL;
	guint16 fw_addr = 0;
	guint16 fw_len = 0;
	guint16 emmc_support_ver = 0;
	guint16 emmc_ver = 0;
	g_autoptr(GInputStream) stream_payload = NULL;
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();

	/* emmc version */
	if (!fu_input_stream_read_u16 (
		stream,
		FU_AG_USBCARDREADER_OFFSET_EMMC_VER,
		&emmc_ver,
		G_LITTLE_ENDIAN,
		error))
		return FALSE;

	if (!fu_input_stream_read_u16 (
		stream,
		FU_AG_USBCARDREADER_OFFSET_FIRMWARE_START_ADDR,
		&fw_addr,
		G_BIG_ENDIAN,
		error))
		return FALSE;

	if (!fu_input_stream_read_u16 (
		stream,
		FU_AG_USBCARDREADER_OFFSET_FIRMWARE_LEN,
		&fw_len,
		G_BIG_ENDIAN,
		error))
		return FALSE;

	/* calculate the offset of the app_ver */
	offset = fw_addr + fw_len - FU_AG_USBCARDREADER_OFFSET_APP_VER_FROM_END;

	/* app version */
	if (!fu_input_stream_read_u16 (
		stream,
		offset,
		&self->app_ver,
		G_BIG_ENDIAN,
		error))
		return FALSE;
	offset+=2;

	/* boot version */
	if (!fu_input_stream_read_u16 (
		stream,
		offset,
		&self->boot_ver,
		G_BIG_ENDIAN,
		error))
		return FALSE;
	offset+= FU_AG_USBCARDREADER_OFFSET_EMMC_SUPPORT_VER_FROM_BOOT_VER;

	/* emmc support version */
	if (!fu_input_stream_read_u16 (
		stream,
		offset,
		&emmc_support_ver,
		G_BIG_ENDIAN,
		error))
		return FALSE;

	if(emmc_ver != emmc_support_ver){
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "EMMC support version is 0x%X while expecting value is 0x%X",
			    emmc_support_ver,
			    emmc_ver);
		return FALSE;
	}

	version = g_strdup_printf("%x",self->app_ver);
	fu_firmware_set_version(firmware, version);

	return TRUE;
}

guint16
fu_algoltek_usbcardreader_firmware_get_boot_ver(FuAlgoltekUsbcardreaderFirmware *self)
{
	g_return_val_if_fail(FU_IS_ALGOLTEK_USBCARDREADER_FIRMWARE(self), G_MAXUINT16);
	return self->boot_ver;
}

static void
fu_algoltek_usbcardreader_firmware_init(FuAlgoltekUsbcardreaderFirmware *self)
{
}

static void
fu_algoltek_usbcardreader_firmware_class_init(FuAlgoltekUsbcardreaderFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_algoltek_usbcardreader_firmware_parse;
	firmware_class->export = fu_algoltek_usbcardreader_firmware_export;
}

FuFirmware *
fu_algoltek_usbcardreader_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ALGOLTEK_USBCARDREADER_FIRMWARE, NULL));
}
