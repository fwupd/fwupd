/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-ep963x-common.h"
#include "fu-ep963x-firmware.h"

struct _FuEp963xFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEp963xFirmware, fu_ep963x_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_ep963x_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	gsize len = 0x0;
	const guint8 *data = g_bytes_get_data(fw, &len);

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

	/* check signature */
	if (memcmp(data + 16, "EP963", 5) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid EP963x binary file");
		return FALSE;
	}

	/* success */
	fu_firmware_set_bytes(firmware, fw);
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
	klass_firmware->parse = fu_ep963x_firmware_parse;
}

FuFirmware *
fu_ep963x_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EP963X_FIRMWARE, NULL));
}
