/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libusb.h>

#include "fu-usb-endpoint.h"
#include "fu-usb-interface.h"

FuUsbInterface *
fu_usb_interface_new(const struct libusb_interface_descriptor *iface, GError **error)
    G_GNUC_NON_NULL(1);
void
fu_usb_interface_add_endpoint(FuUsbInterface *self, FuUsbEndpoint *endpoint);
