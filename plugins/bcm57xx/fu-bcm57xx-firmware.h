/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BCM57XX_FIRMWARE (fu_bcm57xx_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuBcm57xxFirmware, fu_bcm57xx_firmware, FU, BCM57XX_FIRMWARE, FuFirmware)

FuFirmware	*fu_bcm57xx_firmware_new		(void);
guint16		 fu_bcm57xx_firmware_get_vendor		(FuBcm57xxFirmware *self);
guint16		 fu_bcm57xx_firmware_get_model		(FuBcm57xxFirmware *self);
gboolean	 fu_bcm57xx_firmware_is_backup		(FuBcm57xxFirmware *self);
