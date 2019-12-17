/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#include "fu-vli-usbhub-pd-common.h"

#define FU_TYPE_VLI_USBHUB_PD_FIRMWARE (fu_vli_usbhub_pd_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubPdFirmware, fu_vli_usbhub_pd_firmware, FU, VLI_USBHUB_PD_FIRMWARE, FuFirmware)

FuFirmware		*fu_vli_usbhub_pd_firmware_new		(void);
FuVliUsbhubPdChip	 fu_vli_usbhub_pd_firmware_get_chip	(FuVliUsbhubPdFirmware	*self);
guint16			 fu_vli_usbhub_pd_firmware_get_vid	(FuVliUsbhubPdFirmware	*self);
guint16			 fu_vli_usbhub_pd_firmware_get_pid	(FuVliUsbhubPdFirmware	*self);
