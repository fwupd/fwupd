/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-descriptor.h"

#define FU_TYPE_USB_HID_DESCRIPTOR (fu_usb_hid_descriptor_get_type())
G_DECLARE_FINAL_TYPE(FuUsbHidDescriptor,
		     fu_usb_hid_descriptor,
		     FU,
		     USB_HID_DESCRIPTOR,
		     FuUsbDescriptor)
