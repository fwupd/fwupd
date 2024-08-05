/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-common.h"
#include "fu-dell-dock2-dpmux-firmware.h"

#define DOCK_DPMUX_VERSION_OFFSET 0x2019

struct _FuDellDock2DpmuxFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellDock2DpmuxFirmware, fu_dell_dock2_dpmux_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_dock2_dpmux_firmware_parse(FuFirmware *firmware,
				   GInputStream *stream,
				   gsize offset,
				   FwupdInstallFlags flags,
				   GError **error)
{
	guint32 version_raw = 0;
	g_autofree gchar *version_str = NULL;

	if (!fu_input_stream_read_u32(stream,
				      DOCK_DPMUX_VERSION_OFFSET,
				      &version_raw,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;

	version_str = fu_hex_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version_raw(firmware, version_raw);
	fu_firmware_set_version(firmware, version_str);
	return TRUE;
}

static void
fu_dell_dock2_dpmux_firmware_init(FuDellDock2DpmuxFirmware *self)
{
}

static void
fu_dell_dock2_dpmux_firmware_class_init(FuDellDock2DpmuxFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_dock2_dpmux_firmware_parse;
}
