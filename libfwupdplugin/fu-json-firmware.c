/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-json-firmware.h"

/**
 * FuJsonFirmware:
 *
 * A "dummy" loader that just checks if the file can be parsed as JSON format.
 */

G_DEFINE_TYPE(FuJsonFirmware, fu_json_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_json_firmware_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;

	/* just load into memory, no extraction performed */
	json_node = fwupd_json_parser_load_from_stream(json_parser,
						       stream,
						       FWUPD_JSON_LOAD_FLAG_TRUSTED |
							   FWUPD_JSON_LOAD_FLAG_STATIC_KEYS,
						       error);
	if (json_node == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_json_firmware_init(FuJsonFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_json_firmware_class_init(FuJsonFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_json_firmware_parse;
}
