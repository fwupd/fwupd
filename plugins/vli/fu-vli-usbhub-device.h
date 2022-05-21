/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-device.h"

#define FU_TYPE_VLI_USBHUB_DEVICE (fu_vli_usbhub_device_get_type())
G_DECLARE_FINAL_TYPE(FuVliUsbhubDevice, fu_vli_usbhub_device, FU, VLI_USBHUB_DEVICE, FuVliDevice)
