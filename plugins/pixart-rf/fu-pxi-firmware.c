/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-pxi-firmware.h"

#define PIXART_RF_FW_HEADER_SIZE		32	/* bytes */
#define PIXART_RF_FW_HEADER_TAG_OFFSET		24
#define PIXART_RF_FW_HEADER_TAG_SIZE		8	/* bytes */

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
	const guint8 *buf;
	const guint8 tag[] = {
		0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
	};
	gboolean header_ok = TRUE;
	gsize bufsz = 0;
	guint8 fw_header[PIXART_RF_FW_HEADER_SIZE];
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* get buf */
	buf = g_bytes_get_data (fw, &bufsz);
	if (bufsz < sizeof(fw_header)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "firmware invalid, too small!");
		return FALSE;
	}

	/* get fw header */
	if (!fu_memcpy_safe (fw_header, sizeof(fw_header), 0x0,
			     buf, bufsz, bufsz - sizeof(fw_header),
			     sizeof(fw_header), error)) {
		g_prefix_error (error, "failed to read fw header ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "fw header",
				    fw_header, sizeof(fw_header));
	}

	/* check the tag from fw header is correct */
	for (guint32 i = 0x0; i < sizeof(tag); i++) {
		guint8 tmp = 0;
		if (!fu_common_read_uint8_safe (fw_header, sizeof(fw_header),
						i + PIXART_RF_FW_HEADER_TAG_OFFSET,
						&tmp, error))
			return FALSE;
		if (tmp != tag[i]) {
			header_ok = FALSE;
			break;
		}
	}

	/* set the default version if can not find it in fw bin */
	if (header_ok) {
		g_autofree gchar *version = NULL;
		version = g_strdup_printf ("%c.%c.%c",
					   fw_header[0],
					   fw_header[2],
					   fw_header[4]);
		fu_firmware_set_version (firmware, version);
	} else {
		fu_firmware_set_version (firmware, "0.0.0");
	}

	/* success */
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
