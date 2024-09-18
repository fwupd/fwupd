/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-k2-common.h"
#include "fu-dell-k2-pd-firmware.h"

#define DOCK_PD_VERSION_OFFSET 0x46
#define DOCK_PD_VERSION_MAGIC  0x00770064

struct _FuDellK2PdFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellK2PdFirmware, fu_dell_k2_pd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_k2_pd_firmware_find_magic_offset(FuFirmware *firmware,
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
	if (datasz < 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid pd firmware size");
		return FALSE;
	}

	for (gsize addr = 0; addr + 4 < datasz; addr += 1) {
		if (!fu_memread_uint32_safe(data, datasz, addr, &value, G_LITTLE_ENDIAN, error))
			return FALSE;

		if (value == magic) {
			*offset = addr;
			return TRUE;
		}
	}

	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "pd firmware magic not found");
	return FALSE;
}

static gboolean
fu_dell_k2_pd_firmware_set_version(FuFirmware *firmware,
				   GBytes *fw,
				   gsize magic_offset,
				   GError **error)
{
	gsize bufsz = 0;
	gsize version_offset = magic_offset + DOCK_PD_VERSION_OFFSET;
	guint32 raw_version = 0;
	g_autofree gchar *version = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (version_offset + 4 > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware file, offset out of bounds");
		return FALSE;
	}

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    version_offset,
				    &raw_version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	version = fu_version_from_uint32_hex(raw_version, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version(firmware, version);
	return TRUE;
}

static gboolean
fu_dell_k2_pd_firmware_parse(FuFirmware *firmware,
			     GBytes *fw,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	gsize magic_offset = 0;

	/* get offset for the magic */
	if (!fu_dell_k2_pd_firmware_find_magic_offset(firmware,
						      DOCK_PD_VERSION_MAGIC,
						      &magic_offset,
						      error))
		return FALSE;

	/* read version from firmware */
	if (!fu_dell_k2_pd_firmware_set_version(firmware, fw, magic_offset, error))
		return FALSE;

	return TRUE;
}

static void
fu_dell_k2_pd_firmware_init(FuDellK2PdFirmware *self)
{
}

static void
fu_dell_k2_pd_firmware_class_init(FuDellK2PdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_k2_pd_firmware_parse;
}
