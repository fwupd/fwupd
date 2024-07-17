/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libusb.h>

#include "fu-usb-device.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(libusb_context, libusb_exit)

FuUsbDevice *
fu_usb_device_new(FuContext *ctx, libusb_device *usb_device) G_GNUC_NON_NULL(1);
libusb_device *
fu_usb_device_get_dev(FuUsbDevice *self);
