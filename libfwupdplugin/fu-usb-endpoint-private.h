/*
 * Copyright 2020 Emmanuel Pacaud <emmanuel@gnome.org>
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libusb.h>

#include "fu-usb-endpoint.h"

FuUsbEndpoint *
fu_usb_endpoint_new(const struct libusb_endpoint_descriptor *endpoint) G_GNUC_NON_NULL(1);
