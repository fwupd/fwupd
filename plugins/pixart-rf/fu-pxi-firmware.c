/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-pxi-firmware.h"

struct _FuPxiRfFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuPxiRfFirmware, fu_pxi_rf_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_pxi_rf_firmware_parse (FuFirmware *firmware,
			  GBytes *fw,
			  guint64 addr_start,
			  guint64 addr_end,
			  FwupdInstallFlags flags,
			  GError **error)
{
	gsize bufsz = 0;
	guint8 pos = 0;
	const guint8 *buf;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* get buf */
	buf = g_bytes_get_data (fw, &bufsz);
	if (bufsz < 32) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "firmware invalid, too small!");
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "fw last 32 bytes",
				    &buf[bufsz - 32], 32);
	}

	/* find the version tag */
	for (guint32 idx = 0; idx < 32; idx++) {
		if (buf[(bufsz - 32) + idx ] == 'v') {
			pos = idx;
			break;
		}
	}

	/* set the default version if can not find it in fw bin */
	if (pos < 32 - 6 && buf[(bufsz - 32) + pos + 1] == '_') {
		g_autofree gchar *version = NULL;
		version = g_strdup_printf ("%c.%c.%c",
					   buf[(bufsz - 32) + pos + 2],
					   buf[(bufsz - 32) + pos + 4],
					   buf[(bufsz - 32) + pos + 6]);
		fu_firmware_set_version (firmware, version);
	} else {
		fu_firmware_set_version (firmware, "1.0.0");
	}
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_pxi_rf_firmware_init (FuPxiRfFirmware *self)
{
}

static void
fu_pxi_rf_firmware_class_init (FuPxiRfFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_pxi_rf_firmware_parse;
}

FuFirmware *
fu_pxi_rf_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_PXI_RF_FIRMWARE, NULL));
}
