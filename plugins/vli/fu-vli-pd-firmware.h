/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#include "fu-vli-common.h"

#define FU_TYPE_VLI_PD_FIRMWARE (fu_vli_pd_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuVliPdFirmware, fu_vli_pd_firmware, FU, VLI_PD_FIRMWARE, FuFirmware)

FuFirmware		*fu_vli_pd_firmware_new		(void);
FuVliDeviceKind		 fu_vli_pd_firmware_get_kind	(FuVliPdFirmware	*self);
guint16			 fu_vli_pd_firmware_get_vid	(FuVliPdFirmware	*self);
guint16			 fu_vli_pd_firmware_get_pid	(FuVliPdFirmware	*self);
void			 fu_vli_pd_firmware_add_offset	(FuVliPdFirmware	*self,
							 gsize			 offset);
