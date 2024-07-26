/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-ec-firmware.h"

#define DOCK_EC_VERSION_OFFSET 11

struct _FuDellDock2EcFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellDock2EcFirmware, fu_dell_dock2_ec_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_dock2_ec_firmware_find_magic_offset(FuFirmware *firmware, gsize *offset, GError **error)
{
	guint8 magic[] = {0x00, 0x34, 0x12, 0x78, 0x56};
	g_autoptr(GBytes) fw = NULL;
	const guint8 *data;
	gsize datasz = 0;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	data = g_bytes_get_data(fw, &datasz);
	if (datasz < 5 + DOCK_EC_VERSION_OFFSET)
		return FALSE;

	for (gsize addr = 0; addr < datasz; addr += 1) {
		if (!fu_memcmp_safe(data,
				    datasz,
				    addr,
				    magic,
				    sizeof(magic),
				    0,
				    sizeof(magic),
				    NULL))
			continue;

		*offset = addr;
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_dell_dock2_ec_firmware_set_version(FuFirmware *firmware,
				      GInputStream *stream,
				      gsize magic_offset,
				      GError **error)
{
	gsize streamsz = 0;
	gsize version_offset = magic_offset - DOCK_EC_VERSION_OFFSET;
	guint8 raw_version[DOCK_EC_VERSION_OFFSET] = {'\0'};
	g_autofree const gchar *ver = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	if (!fu_input_stream_read_safe(stream,
				       (guint8 *)raw_version,
				       sizeof(raw_version),
				       0,
				       version_offset,
				       sizeof(raw_version),
				       error))
		return FALSE;

	ver = g_strndup((const gchar *)raw_version, DOCK_EC_VERSION_OFFSET);
	fu_firmware_set_version(firmware, ver);
	return TRUE;
}

static gboolean
fu_dell_dock2_ec_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	gsize magic_offset = 0;

	/* get offset for the magic */
	if (!fu_dell_dock2_ec_firmware_find_magic_offset(firmware, &magic_offset, error))
		return FALSE;

	/* read version from firmware */
	if (!fu_dell_dock2_ec_firmware_set_version(firmware, stream, magic_offset, error))
		return FALSE;

	return TRUE;
}

static void
fu_dell_dock2_ec_firmware_init(FuDellDock2EcFirmware *self)
{
}

static void
fu_dell_dock2_ec_firmware_class_init(FuDellDock2EcFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_dock2_ec_firmware_parse;
}
