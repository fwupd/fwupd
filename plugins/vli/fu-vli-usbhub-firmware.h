/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-usbhub-common.h"

#define FU_TYPE_VLI_USBHUB_FIRMWARE (fu_vli_usbhub_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuVliUsbhubFirmware,
		     fu_vli_usbhub_firmware,
		     FU,
		     VLI_USBHUB_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_vli_usbhub_firmware_new(void);
FuVliDeviceKind
fu_vli_usbhub_firmware_get_device_kind(FuVliUsbhubFirmware *self);
guint16
fu_vli_usbhub_firmware_get_device_id(FuVliUsbhubFirmware *self);
