/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-usb-bos-descriptor.h"

FuUsbBosDescriptor *
fu_usb_bos_descriptor_new(FuUsbBosHdr *st_hdr) G_GNUC_NON_NULL(1);
