/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PARADE_USBHUB_DEVICE (fu_parade_usbhub_device_get_type())
G_DECLARE_FINAL_TYPE(FuParadeUsbhubDevice,
		     fu_parade_usbhub_device,
		     FU,
		     PARADE_USBHUB_DEVICE,
		     FuUsbDevice)
