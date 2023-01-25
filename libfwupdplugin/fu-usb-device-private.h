/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-usb-device.h"

#define FU_USB_DEVICE_EMULATION_TAG "org.freedesktop.fwupd.emulation.v1"

const gchar *
fu_usb_device_get_platform_id(FuUsbDevice *self);
