/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-hid-descriptor.h"

FuUsbHidDescriptor *
fu_usb_hid_descriptor_new(void);
guint8
fu_usb_hid_descriptor_get_iface_number(FuUsbHidDescriptor *self) G_GNUC_NON_NULL(1);
void
fu_usb_hid_descriptor_set_iface_number(FuUsbHidDescriptor *self, guint8 iface_number)
    G_GNUC_NON_NULL(1);
gsize
fu_usb_hid_descriptor_get_descriptor_length(FuUsbHidDescriptor *self) G_GNUC_NON_NULL(1);
GBytes *
fu_usb_hid_descriptor_get_blob(FuUsbHidDescriptor *self) G_GNUC_NON_NULL(1);
void
fu_usb_hid_descriptor_set_blob(FuUsbHidDescriptor *self, GBytes *blob) G_GNUC_NON_NULL(1, 2);
