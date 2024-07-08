/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-parade-usbhub-common.h"
#include "fu-parade-usbhub-firmware.h"
#include "fu-parade-usbhub-struct.h"

struct _FuParadeUsbhubFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuParadeUsbhubFirmware, fu_parade_usbhub_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_parade_usbhub_validate(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_parade_usbhub_hdr_validate_bytes(fw, offset, error);
}

static gboolean
fu_parade_usbhub_firmware_parse(FuFirmware *firmware,
				GBytes *fw,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	gsize bufsz = 0;
	guint32 version_raw = 0;
	g_autofree gchar *version = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (bufsz != FU_PARADE_USBHUB_SPI_ROM_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "wrong file size, expected 0x%x and got 0x%x",
			    (guint)FU_PARADE_USBHUB_SPI_ROM_SIZE,
			    (guint)bufsz);
		return FALSE;
	}

	/* read out FW#1 version */
	if (!fu_memread_uint32_safe(buf, bufsz, 0x41000, &version_raw, G_LITTLE_ENDIAN, error))
		version = fu_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version_raw(firmware, version_raw);
	fu_firmware_set_version(firmware, version);

	/* success */
	return TRUE;
}

static void
fu_parade_usbhub_firmware_init(FuParadeUsbhubFirmware *self)
{
}

static void
fu_parade_usbhub_firmware_class_init(FuParadeUsbhubFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->check_magic = fu_parade_usbhub_validate;
	firmware_class->parse = fu_parade_usbhub_firmware_parse;
}

FuFirmware *
fu_parade_usbhub_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PARADE_USBHUB_FIRMWARE, NULL));
}
