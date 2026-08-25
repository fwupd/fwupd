/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-descriptor.h"

G_BEGIN_DECLS

#define FU_TYPE_USB_BOS_DESCRIPTOR (fu_usb_bos_descriptor_get_type())
G_DECLARE_FINAL_TYPE(FuUsbBosDescriptor,
		     fu_usb_bos_descriptor,
		     FU,
		     USB_BOS_DESCRIPTOR,
		     FuUsbDescriptor)

guint8
fu_usb_bos_descriptor_get_capability(FuUsbBosDescriptor *self) G_GNUC_PURE;

G_END_DECLS
