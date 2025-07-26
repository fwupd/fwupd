/*
 * Copyright 2024 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-kria-image-firmware.h"

struct _FuAmdKriaImageFirmware {
	FuFirmware parent_instance;
};

#define VERSION_OFFSET 0x70
#define VERSION_SIZE   0x24

G_DEFINE_TYPE(FuAmdKriaImageFirmware, fu_amd_kria_image_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_amd_kria_image_firmware_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	const gchar *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) fw = NULL;
	g_autofree gchar *version = NULL;

	fw = fu_input_stream_read_bytes(stream, VERSION_OFFSET, VERSION_SIZE, NULL, error);
	if (fw == NULL)
		return FALSE;

	buf = g_bytes_get_data(fw, &bufsz);

	version = fu_strsafe(buf, bufsz);
	if (version == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_SIGNATURE_INVALID,
				    "no valid version");
		return FALSE;
	}

	fu_firmware_set_version(FU_FIRMWARE(firmware), version);

	return TRUE;
}

static void
fu_amd_kria_image_firmware_init(FuAmdKriaImageFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_amd_kria_image_firmware_class_init(FuAmdKriaImageFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_amd_kria_image_firmware_parse;
}

FuFirmware *
fu_amd_kria_image_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_KRIA_IMAGE_FIRMWARE, NULL));
}
