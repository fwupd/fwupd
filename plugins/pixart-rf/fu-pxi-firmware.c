/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "fu-common.h"
#include "fu-ihex-firmware.h"
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
	
	gsize fw_sz = 0;
	guint8 pos = 0;
	gchar version[5] = {'.', '.', '.', '.', '.'};
	guint32 idx = 0;
	const guint8 *fw_ptr;
	g_autoptr(FuFirmwareImage) img = NULL;

	/* get the default bytes and fw sz */
	fw_sz = g_bytes_get_size (fw);
	fw_ptr = g_bytes_get_data (fw, &fw_sz);
	
	fu_common_dump_raw (G_LOG_DOMAIN, "fw last 32 bytes", &fw_ptr[fw_sz - 32], 32);

	/* find the version tag */
	for (idx = 0; idx < 32; idx++) {
		if(fw_ptr[(fw_sz - 32) + idx ] == 'v') {
		        pos = idx;
		        break;
		}
	}

	/* ensure the version */
	if ((pos != 31) && (fw_ptr[(fw_sz - 32) + pos + 1] == '_')) {
	
		g_debug ("version in bin");
		version[0] = fw_ptr[(fw_sz - 32) + pos + 2];
		version[2] = fw_ptr[(fw_sz - 32) + pos + 4];
		version[4] = fw_ptr[(fw_sz - 32) + pos + 6];
		fu_firmware_set_version (firmware, version);
		
	} else {
		/* set the default version if can not find it in fw bin */
		fu_firmware_set_version (firmware, "1.0.0");
	}

	
	g_debug ("fu firmware version %s", fu_firmware_get_version (firmware));
	
	img = fu_firmware_image_new (fw);
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
