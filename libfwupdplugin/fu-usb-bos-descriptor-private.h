/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libusb.h>

#include "fu-usb-bos-descriptor.h"

FuUsbBosDescriptor *
fu_usb_bos_descriptor_new(const struct libusb_bos_dev_capability_descriptor *bos_cap)
    G_GNUC_NON_NULL(1);
