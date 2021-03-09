/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"
#include "fu-ccgx-dmc-common.h"

#define FU_TYPE_CCGX_DMC_FIRMWARE (fu_ccgx_dmc_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxDmcFirmware, fu_ccgx_dmc_firmware, FU,CCGX_DMC_FIRMWARE, FuFirmware)

typedef struct {
	guint16			 start_row;
	guint16			 num_rows;
	GPtrArray		*data_records;
} FuCcgxDmcFirmwareSegmentRecord;

typedef struct {
	guint8			 row_size;
	guint32			 img_offset;
	guint8			 img_digest[32];
	guint8			 num_img_segments;
	GPtrArray		*seg_records;
} FuCcgxDmcFirmwareRecord;

FuFirmware	*fu_ccgx_dmc_firmware_new			(void);
GPtrArray	*fu_ccgx_dmc_firmware_get_image_records		(FuCcgxDmcFirmware	*self);
GBytes		*fu_ccgx_dmc_firmware_get_fwct_record		(FuCcgxDmcFirmware	*self);
GBytes		*fu_ccgx_dmc_firmware_get_custom_meta_record	(FuCcgxDmcFirmware	*self);
guint32		 fu_ccgx_dmc_firmware_get_fw_data_size		(FuCcgxDmcFirmware	*self);
