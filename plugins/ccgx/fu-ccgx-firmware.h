/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-ccgx-common.h"

#define FU_TYPE_CCGX_FIRMWARE (fu_ccgx_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxFirmware, fu_ccgx_firmware, FU,CCGX_FIRMWARE, FuFirmware)

typedef struct {
	guint8		 array_id;
	guint16		 row_number;
	GBytes		*data;
} FuCcgxFirmwareRecord;

FuFirmware	*fu_ccgx_firmware_new			(void);
GPtrArray	*fu_ccgx_firmware_get_records		(FuCcgxFirmware	*self);
guint16		 fu_ccgx_firmware_get_app_type		(FuCcgxFirmware	*self);
guint16		 fu_ccgx_firmware_get_silicon_id	(FuCcgxFirmware	*self);
FWMode		 fu_ccgx_firmware_get_fw_mode		(FuCcgxFirmware	*self);
