/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-common.h"
#include "fu-dell-dock2-ilan-firmware.h"

#define DOCK_ILAN_VERSION_OFFSET 0x0A

struct _FuDellDock2IlanFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellDock2IlanFirmware, fu_dell_dock2_ilan_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_dock2_ilan_firmware_parse(FuFirmware *firmware,
				  GInputStream *stream,
				  gsize offset,
				  FwupdInstallFlags flags,
				  GError **error)
{
	guint16 version_raw = 0;
	guint8 version_major = 0;
	guint8 version_minor = 0;
	g_autofree gchar *version_str = NULL;

	if (!fu_input_stream_read_u16(stream,
				      DOCK_ILAN_VERSION_OFFSET,
				      &version_raw,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	version_major = (version_raw >> 12) & 0xff;
	version_minor = version_raw & 0xff;
	version_str = g_strdup_printf("%x.%x", version_major, version_minor);
	fu_firmware_set_version(firmware, version_str);
	return TRUE;
}

static void
fu_dell_dock2_ilan_firmware_init(FuDellDock2IlanFirmware *self)
{
}

static void
fu_dell_dock2_ilan_firmware_class_init(FuDellDock2IlanFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_dock2_ilan_firmware_parse;
}
