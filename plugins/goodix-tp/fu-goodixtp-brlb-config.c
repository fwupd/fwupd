/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixtp-brlb-config.h"
#include "fu-goodixtp-struct.h"

struct _FuGoodixtpBrlbConfig {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpBrlbConfig, fu_goodixtp_brlb_config, FU_TYPE_FIRMWARE)

static gboolean
fu_goodixtp_brlb_config_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FuFirmwareParseFlags flags,
			      GError **error)
{
	gsize streamsz = 0;
	guint8 cfg_ver = 0;

	/* sanity check */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz <= 34) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "config is too short");
		return FALSE;
	}

	/* get version */
	if (!fu_input_stream_read_u8(stream, 34, &cfg_ver, error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, cfg_ver);

	/* success */
	return TRUE;
}

static void
fu_goodixtp_brlb_config_init(FuGoodixtpBrlbConfig *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_set_idx(FU_FIRMWARE(self), 4);
	fu_firmware_set_addr(FU_FIRMWARE(self), 0x40000);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 10 * FU_KB);
}

static void
fu_goodixtp_brlb_config_class_init(FuGoodixtpBrlbConfigClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_goodixtp_brlb_config_parse;
}

FuFirmware *
fu_goodixtp_brlb_config_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GOODIXTP_BRLB_CONFIG, NULL));
}
