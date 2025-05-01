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
fu_parade_usbhub_firmware_validate(FuFirmware *firmware,
				   GInputStream *stream,
				   gsize offset,
				   GError **error)
{
	return fu_struct_parade_usbhub_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_parade_usbhub_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				FuFirmwareParseFlags flags,
				GError **error)
{
	gsize streamsz = 0;
	guint32 version_raw = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz != FU_PARADE_USBHUB_SPI_ROM_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "wrong file size, expected 0x%x and got 0x%x",
			    (guint)FU_PARADE_USBHUB_SPI_ROM_SIZE,
			    (guint)streamsz);
		return FALSE;
	}

	/* read out FW#1 version */
	if (!fu_input_stream_read_u32(stream, 0x41000, &version_raw, G_LITTLE_ENDIAN, error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, version_raw);

	/* success */
	return TRUE;
}

static gchar *
fu_parade_usbhub_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_parade_usbhub_firmware_init(FuParadeUsbhubFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_parade_usbhub_firmware_class_init(FuParadeUsbhubFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_parade_usbhub_firmware_convert_version;
	firmware_class->validate = fu_parade_usbhub_firmware_validate;
	firmware_class->parse = fu_parade_usbhub_firmware_parse;
}

FuFirmware *
fu_parade_usbhub_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PARADE_USBHUB_FIRMWARE, NULL));
}
