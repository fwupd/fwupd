/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 Emmanuel Pacaud <emmanuel@gnome.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-descriptor.h"

#define FU_TYPE_USB_INTERFACE (fu_usb_interface_get_type())
G_DECLARE_FINAL_TYPE(FuUsbInterface, fu_usb_interface, FU, USB_INTERFACE, FuUsbDescriptor)

guint8
fu_usb_interface_get_number(FuUsbInterface *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_interface_get_alternate(FuUsbInterface *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_interface_get_class(FuUsbInterface *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_interface_get_subclass(FuUsbInterface *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_interface_get_protocol(FuUsbInterface *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_interface_get_index(FuUsbInterface *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_usb_interface_get_endpoints(FuUsbInterface *self) G_GNUC_NON_NULL(1);
