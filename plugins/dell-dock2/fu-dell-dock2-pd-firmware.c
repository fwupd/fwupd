/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-common.h"
#include "fu-dell-dock2-pd-firmware.h"

#define DOCK_PD_VERSION_OFFSET 0x46
#define DOCK_PD_VERSION_MAGIC  0x00770064

struct _FuDellDock2PdFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellDock2PdFirmware, fu_dell_dock2_pd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_dock2_pd_firmware_find_magic_offset(FuFirmware *firmware,
					    guint32 magic,
					    gsize *offset,
					    GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	const guint8 *data;
	gsize datasz = 0;
	guint32 value;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	data = g_bytes_get_data(fw, &datasz);
	if (datasz < 4)
		return FALSE;

	for (gsize addr = 0; addr < datasz; addr += 1) {
		if (!fu_memread_uint32_safe(data, datasz, addr, &value, G_LITTLE_ENDIAN, error))
			return FALSE;

		if (value == magic) {
			*offset = addr;
			return TRUE;
		}

		if (addr + 4 >= datasz)
			return FALSE;
	}
	return FALSE;
}

static gboolean
fu_dell_dock2_pd_firmware_set_version(FuFirmware *firmware,
				      GInputStream *stream,
				      gsize magic_offset,
				      GError **error)
{
	gsize streamsz = 0;
	gsize version_offset = magic_offset + DOCK_PD_VERSION_OFFSET;
	guint32 raw_version = 0;
	g_autofree gchar *ver = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	if (version_offset + 4 > streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware file, offset out of bounds");
		return FALSE;
	}

	if (!fu_input_stream_read_u32(stream, version_offset, &raw_version, G_LITTLE_ENDIAN, error))
		return FALSE;

	ver = fu_hex_version_from_uint32(raw_version, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version(firmware, ver);
	fu_firmware_set_version_raw(firmware, raw_version);

	return TRUE;
}

static gboolean
fu_dell_dock2_pd_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	gsize magic_offset = 0;

	/* get offset for the magic */
	if (!fu_dell_dock2_pd_firmware_find_magic_offset(firmware,
							 DOCK_PD_VERSION_MAGIC,
							 &magic_offset,
							 error))
		return FALSE;

	/* read version from firmware */
	if (!fu_dell_dock2_pd_firmware_set_version(firmware, stream, magic_offset, error))
		return FALSE;

	return TRUE;
}

static void
fu_dell_dock2_pd_firmware_init(FuDellDock2PdFirmware *self)
{
}

static void
fu_dell_dock2_pd_firmware_class_init(FuDellDock2PdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_dock2_pd_firmware_parse;
}
