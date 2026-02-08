/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-novatek-ts-firmware.h"

struct _FuNovatekTsFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuNovatekTsFirmware, fu_novatek_ts_firmware, FU_TYPE_FIRMWARE)

#define FW_BIN_END_FLAG_STR "NVT"

static gboolean
fu_novatek_ts_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	gsize streamsz = 0;
	g_autofree gchar *endflag = NULL;

	/* check the end flag exists */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < strlen(FW_BIN_END_FLAG_STR)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "end flag not possible");
		return FALSE;
	}
	endflag = fu_input_stream_read_string(stream,
					      streamsz - strlen(FW_BIN_END_FLAG_STR),
					      strlen(FW_BIN_END_FLAG_STR),
					      error);
	if (endflag == NULL)
		return FALSE;
	if (g_strcmp0(endflag, FW_BIN_END_FLAG_STR) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "end flag not found (expected %s, got %s)",
			    FW_BIN_END_FLAG_STR,
			    endflag);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_novatek_ts_firmware_init(FuNovatekTsFirmware *self)
{
	fu_firmware_set_size_max(FU_FIRMWARE(self), 1024 * 320);
}

static void
fu_novatek_ts_firmware_class_init(FuNovatekTsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_novatek_ts_firmware_parse;
}
