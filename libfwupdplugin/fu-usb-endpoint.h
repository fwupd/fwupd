/*
 * Copyright 2020 Emmanuel Pacaud <emmanuel@gnome.org>
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-descriptor.h"

#define FU_TYPE_USB_ENDPOINT (fu_usb_endpoint_get_type())
G_DECLARE_FINAL_TYPE(FuUsbEndpoint, fu_usb_endpoint, FU, USB_ENDPOINT, FuUsbDescriptor)

guint16
fu_usb_endpoint_get_maximum_packet_size(FuUsbEndpoint *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_endpoint_get_polling_interval(FuUsbEndpoint *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_endpoint_get_address(FuUsbEndpoint *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_endpoint_get_number(FuUsbEndpoint *self) G_GNUC_NON_NULL(1);
FuUsbDirection
fu_usb_endpoint_get_direction(FuUsbEndpoint *self) G_GNUC_NON_NULL(1);
