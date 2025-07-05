/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 * Copyright 2023 Ben Chuang <benchuanggli@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
				 GInputStream *stream,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	guint8 ver[4] = {0};
	g_autofree gchar *version = NULL;

	/* version */
	if (!fu_input_stream_read_safe(stream,
				       ver,
				       sizeof(ver),
				       0x0,
				       FU_GENESYS_GL32XX_VERSION_ADDR,
				       sizeof(ver),
				       error))
		return FALSE;
	version = g_strdup_printf("%c%c%c%c", ver[0x0], ver[0x1], ver[0x2], ver[0x3]);
	fu_firmware_set_version(firmware, version);

	/* verify checksum */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		gsize streamsz = 0;
		guint8 chksum_actual = 0;
		guint8 chksum_expected = 0;
		g_autoptr(GInputStream) stream_tmp = NULL;

		if (!fu_input_stream_size(stream, &streamsz, error))
			return FALSE;
		if (streamsz < 2) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "image is too small");
			return FALSE;
		}
		if (!fu_input_stream_read_u8(stream, streamsz - 1, &chksum_expected, error))
			return FALSE;
		stream_tmp = fu_partial_input_stream_new(stream, 0, streamsz - 2, error);
		if (stream_tmp == NULL)
			return FALSE;
		if (!fu_input_stream_compute_sum8(stream_tmp, &chksum_actual, error))
			return FALSE;
		if (FU_GENESYS_GL32XX_CHECKSUM_MAGIC - chksum_actual != chksum_expected) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "checksum mismatch, got 0x%02x, expected 0x%02x",
				    chksum_actual,
				    chksum_expected);
			return FALSE;
		}
	}

	/* success */
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
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_genesys_gl32xx_firmware_parse;
}

FuFirmware *
fu_genesys_gl32xx_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_GL32XX_FIRMWARE, NULL));
}
