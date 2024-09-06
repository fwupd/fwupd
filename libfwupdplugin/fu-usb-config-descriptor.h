/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-descriptor.h"

#define FU_TYPE_USB_CONFIG_DESCRIPTOR (fu_usb_config_descriptor_get_type())
G_DECLARE_FINAL_TYPE(FuUsbConfigDescriptor,
		     fu_usb_config_descriptor,
		     FU,
		     USB_CONFIG_DESCRIPTOR,
		     FuUsbDescriptor)

guint8
fu_usb_config_descriptor_get_configuration(FuUsbConfigDescriptor *self);
guint8
fu_usb_config_descriptor_get_configuration_value(FuUsbConfigDescriptor *self);
