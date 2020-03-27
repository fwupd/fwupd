/*
 * Copyright (C) 2017 Intel Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_THUNDERBOLT_FIRMWARE (fu_thunderbolt_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuThunderboltFirmware, fu_thunderbolt_firmware, FU,THUNDERBOLT_FIRMWARE, FuFirmware)

/* byte offsets in firmware image */
#define FU_TBT_OFFSET_NATIVE		0x7B
#define FU_TBT_CHUNK_SZ			0x40

FuThunderboltFirmware *fu_thunderbolt_firmware_new	(void);
gboolean	 fu_thunderbolt_firmware_is_host	(FuThunderboltFirmware	*self);
gboolean	 fu_thunderbolt_firmware_is_native	(FuThunderboltFirmware	*self);
gboolean	 fu_thunderbolt_firmware_get_has_pd	(FuThunderboltFirmware	*self);
guint16		 fu_thunderbolt_firmware_get_device_id	(FuThunderboltFirmware	*self);
guint16		 fu_thunderbolt_firmware_get_vendor_id	(FuThunderboltFirmware	*self);
guint16		 fu_thunderbolt_firmware_get_model_id	(FuThunderboltFirmware	*self);
guint8		 fu_thunderbolt_firmware_get_flash_size	(FuThunderboltFirmware	*self);
