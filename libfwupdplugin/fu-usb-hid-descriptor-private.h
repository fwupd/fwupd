/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-hid-descriptor.h"

G_BEGIN_DECLS

guint8
fu_usb_hid_descriptor_get_iface_number(FuUsbHidDescriptor *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
void
fu_usb_hid_descriptor_set_iface_number(FuUsbHidDescriptor *self, guint8 iface_number)
    G_GNUC_NON_NULL(1);
gsize
fu_usb_hid_descriptor_get_descriptor_length(FuUsbHidDescriptor *self)
    G_GNUC_NON_NULL(1) G_GNUC_PURE;
GBytes *
fu_usb_hid_descriptor_get_blob(FuUsbHidDescriptor *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
void
fu_usb_hid_descriptor_set_blob(FuUsbHidDescriptor *self, GBytes *blob) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
