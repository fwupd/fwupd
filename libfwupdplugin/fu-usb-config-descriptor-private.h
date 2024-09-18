/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libusb.h>

#include "fu-usb-config-descriptor.h"

FuUsbConfigDescriptor *
fu_usb_config_descriptor_new(void);
