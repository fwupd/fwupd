/*
 * Copyright (C) 2020 Fresco Logic
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_FRESCO_PD_FIRMWARE (fu_fresco_pd_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuFrescoPdFirmware, fu_fresco_pd_firmware, FU, FRESCO_PD_FIRMWARE, FuFirmware)

FuFirmware		*fu_fresco_pd_firmware_new			(void);
guint8			 fu_fresco_pd_firmware_get_customer_id		(FuFrescoPdFirmware	*self);
