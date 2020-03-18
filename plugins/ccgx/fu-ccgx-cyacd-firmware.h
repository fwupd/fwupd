/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-ccgx-common.h"
#include "fu-ccgx-cyacd-file.h"

#define FU_TYPE_CCGX_CYACD_FIRMWARE (fu_ccgx_cyacd_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxCyacdFirmware, fu_ccgx_cyacd_firmware, FU,CCGX_CYACD_FIRMWARE, FuFirmware)

FuFirmware	*fu_ccgx_cyacd_firmware_new		(void);
void		 fu_ccgx_cyacd_firmware_set_device_info	(FuCcgxCyacdFirmware	*self,
							 FWImageType		 fw_image_type,
							 guint16		 silicon_id,
							 guint16		 app_type);
guint32		 fu_ccgx_cyacd_firmware_get_info_count	(FuCcgxCyacdFirmware	*self);
CyacdFileInfo	*fu_ccgx_cyacd_firmware_get_info_data	(FuCcgxCyacdFirmware	*self,
							 guint32		 index);
