/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-k2-common.h"
#include "fu-dell-k2-dpmux-firmware.h"

#define DOCK_DPMUX_VERSION_OFFSET 0x2019

struct _FuDellK2DpmuxFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellK2DpmuxFirmware, fu_dell_k2_dpmux_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_k2_dpmux_firmware_parse(FuFirmware *firmware,
				GBytes *fw,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	guint32 version_raw = 0;
	gsize bufsz = 0;
	g_autofree gchar *version = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    DOCK_DPMUX_VERSION_OFFSET,
				    &version_raw,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;

	version = fu_version_from_uint32_hex(version_raw, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version(firmware, version);
	return TRUE;
}

static void
fu_dell_k2_dpmux_firmware_init(FuDellK2DpmuxFirmware *self)
{
}

static void
fu_dell_k2_dpmux_firmware_class_init(FuDellK2DpmuxFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_k2_dpmux_firmware_parse;
}
