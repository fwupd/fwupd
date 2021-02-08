/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-pxi-firmware.h"

#define PIXART_RF_FW_HEADER_SIZE	32	/* bytes */
#define PIXART_RF_FW_TAG_OFFSET		24	

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
	const guint8 *buf;
	guint8 fw_header[PIXART_RF_FW_HEADER_SIZE];
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);
	const guint8 TAG[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
	gboolean check_header_exist = TRUE;

	/* get buf */
	buf = g_bytes_get_data (fw, &bufsz);
	if (bufsz < PIXART_RF_FW_HEADER_SIZE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "firmware invalid, too small!");
		return FALSE;
	}
	/* get fw header */
	if (!fu_memcpy_safe (fw_header, PIXART_RF_FW_HEADER_SIZE, 0x0,
			     buf, bufsz, bufsz - PIXART_RF_FW_HEADER_SIZE,
			     PIXART_RF_FW_HEADER_SIZE, error)) {
		g_prefix_error (error, "failed to read fw header: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "fw header",
				    fw_header, PIXART_RF_FW_HEADER_SIZE);
	}

	/* check the TAG from fw header is correct */
	for (guint32 idx = PIXART_RF_FW_TAG_OFFSET; idx < PIXART_RF_FW_HEADER_SIZE; idx++) {
		if (fw_header[idx] != TAG[idx - 24]) {
			check_header_exist = FALSE;
			break;
		}
	}

	/* set the default version if can not find it in fw bin */
	if (check_header_exist) {
		g_autofree gchar *version = NULL;
		version = g_strdup_printf ("%c.%c.%c",
					   fw_header[0],
					   fw_header[2],
					   fw_header[4]);
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
