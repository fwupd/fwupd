/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2023 Ben Chuang <benchuanggli@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-genesys-gl32xx-firmware.h"

#define FU_GENESYS_GL32XX_VERSION_ADDR	 0x00D4
#define FU_GENESYS_GL32XX_CHECKSUM_MAGIC 0x0055

struct _FuGenesysGl32xxFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuGenesysGl32xxFirmware, fu_genesys_gl32xx_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_genesys_gl32xx_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	gsize bufsz = 0;
	guint8 ver[4] = {0};
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version = NULL;

	/* version */
	if (!fu_memcpy_safe(ver,
			    sizeof(ver),
			    0x0,
			    buf,
			    g_bytes_get_size(fw),
			    FU_GENESYS_GL32XX_VERSION_ADDR,
			    sizeof(ver),
			    error))
		return FALSE;
	version = g_strdup_printf("%c%c%c%c", ver[0x0], ver[0x1], ver[0x2], ver[0x3]);
	fu_firmware_set_version(firmware, version);

	/* verify checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 chksum_expected = buf[bufsz - 1];
		guint8 chksum_actual = FU_GENESYS_GL32XX_CHECKSUM_MAGIC - fu_sum8(buf, bufsz - 2);
		if (chksum_actual != chksum_expected) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "checksum mismatch, got 0x%02x, expected 0x%02x",
				    chksum_actual,
				    chksum_expected);
			return FALSE;
		}
	}

	/* payload is entire blob */
	fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static void
fu_genesys_gl32xx_firmware_init(FuGenesysGl32xxFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_genesys_gl32xx_firmware_class_init(FuGenesysGl32xxFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_genesys_gl32xx_firmware_parse;
}

FuFirmware *
fu_genesys_gl32xx_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_GL32XX_FIRMWARE, NULL));
}
