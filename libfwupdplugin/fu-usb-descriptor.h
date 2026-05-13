/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-usb-struct.h"

#define FU_TYPE_USB_DESCRIPTOR (fu_usb_descriptor_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUsbDescriptor, fu_usb_descriptor, FU, USB_DESCRIPTOR, FuFirmware)

struct _FuUsbDescriptorClass {
	FuFirmwareClass parent_class;
};
