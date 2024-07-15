/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-usb-struct.h"

#define FU_TYPE_USB_BOS_DESCRIPTOR (fu_usb_bos_descriptor_get_type())
G_DECLARE_FINAL_TYPE(FuUsbBosDescriptor, fu_usb_bos_descriptor, FU, USB_BOS_DESCRIPTOR, FuFirmware)

guint8
fu_usb_bos_descriptor_get_capability(FuUsbBosDescriptor *self);
GBytes *
fu_usb_bos_descriptor_get_extra(FuUsbBosDescriptor *self);
