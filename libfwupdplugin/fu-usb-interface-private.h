/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libusb.h>

#include "fu-usb-interface.h"

FuUsbInterface *
fu_usb_interface_new(const struct libusb_interface_descriptor *iface) G_GNUC_NON_NULL(1);
