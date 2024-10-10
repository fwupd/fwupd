/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-k2-common.h"
#include "fu-dell-k2-wtpd-firmware.h"

#define DOCK_WTPD_VERSION_OFFSET 0x2019

struct _FuDellK2WtpdFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellK2WtpdFirmware, fu_dell_k2_wtpd_firmware, FU_TYPE_FIRMWARE)

static gchar *
fu_dell_k2_wtpd_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_firmware_get_version_format(firmware));
}

static gboolean
fu_dell_k2_wtpd_firmware_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       FwupdInstallFlags flags,
			       GError **error)
{
	guint32 version_raw = 0;

	if (!fu_input_stream_read_u32(stream,
				      DOCK_WTPD_VERSION_OFFSET,
				      &version_raw,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;

	fu_firmware_set_version_raw(firmware, version_raw);
	return TRUE;
}

static void
fu_dell_k2_wtpd_firmware_init(FuDellK2WtpdFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_dell_k2_wtpd_firmware_class_init(FuDellK2WtpdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_k2_wtpd_firmware_parse;
	firmware_class->convert_version = fu_dell_k2_wtpd_firmware_convert_version;
}
