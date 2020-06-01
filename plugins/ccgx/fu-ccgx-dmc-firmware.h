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
	GBytes			*data;
} FuCcgxDmcFirmwareDataRecord;

typedef struct {
	FwctSegmentationInfo	 info_header;
	GPtrArray		*data_records;
} FuCcgxDmcFirmwareSegmentRecord;

typedef struct {
	FwctImageInfo		 info_header;
	GPtrArray		*seg_records;
} FuCcgxDmcFirmwareImageRecord;

FuFirmware	*fu_ccgx_dmc_firmware_new			(void);
GPtrArray	*fu_ccgx_dmc_firmware_get_image_records		(FuCcgxDmcFirmware	*self);
GBytes		*fu_ccgx_dmc_firmware_get_fwct_record		(FuCcgxDmcFirmware	*self);
GBytes		*fu_ccgx_dmc_firmware_get_custom_meta_record	(FuCcgxDmcFirmware	*self);
FwctInfo	*fu_ccgx_dmc_firmware_get_fwct_info		(FuCcgxDmcFirmware	*self);
guint32		 fu_ccgx_dmc_firmware_get_fw_data_size		(FuCcgxDmcFirmware	*self);
