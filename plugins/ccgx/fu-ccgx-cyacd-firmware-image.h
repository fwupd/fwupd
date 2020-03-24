/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware-image.h"

#define FU_TYPE_CCGX_CYACD_FIRMWARE_IMAGE (fu_ccgx_cyacd_firmware_image_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxCyacdFirmwareImage, fu_ccgx_cyacd_firmware_image, FU, CCGX_CYACD_FIRMWARE_IMAGE, FuFirmwareImage)

typedef struct {
	guint8		 array_id;
	guint16		 row_number;
	GBytes		*data;
} FuCcgxCyacdFirmwareImageRecord;

FuFirmwareImage	*fu_ccgx_cyacd_firmware_image_new		(void);
gboolean	 fu_ccgx_cyacd_firmware_image_parse_header	(FuCcgxCyacdFirmwareImage	*self,
								 const gchar			*line,
								 GError				**error);
gboolean	 fu_ccgx_cyacd_firmware_image_parse_md_block	(FuCcgxCyacdFirmwareImage	*self,
								 GError				**error);
gboolean	 fu_ccgx_cyacd_firmware_image_add_record	(FuCcgxCyacdFirmwareImage	*self,
								 const gchar			*line,
								 GError				**error);
GPtrArray	*fu_ccgx_cyacd_firmware_image_get_records	(FuCcgxCyacdFirmwareImage	*self);
guint16		 fu_ccgx_cyacd_firmware_image_get_app_type	(FuCcgxCyacdFirmwareImage	*self);
