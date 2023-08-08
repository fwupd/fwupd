/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-ep963x-common.h"
#include "fu-ep963x-firmware.h"

struct _FuEp963xFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEp963xFirmware, fu_ep963x_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_ep963x_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint8 magic[5] = "EP963";
	return fu_memcmp_safe(g_bytes_get_data(fw, NULL),
			      g_bytes_get_size(fw),
			      offset + 16,
			      magic,
			      sizeof(magic),
			      0x0,
			      sizeof(magic),
			      error);
}

static gboolean
fu_ep963x_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	gsize len = g_bytes_get_size(fw);

	/* check size */
	if (len != FU_EP963_FIRMWARE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size expected 0x%x, got 0x%x",
			    (guint)FU_EP963_FIRMWARE_SIZE,
			    (guint)len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_ep963x_firmware_init(FuEp963xFirmware *self)
{
}

static void
fu_ep963x_firmware_class_init(FuEp963xFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_ep963x_firmware_check_magic;
	klass_firmware->parse = fu_ep963x_firmware_parse;
}
