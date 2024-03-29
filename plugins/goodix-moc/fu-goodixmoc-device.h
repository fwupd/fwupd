/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GOODIXMOC_DEVICE (fu_goodixmoc_device_get_type())
G_DECLARE_FINAL_TYPE(FuGoodixMocDevice, fu_goodixmoc_device, FU, GOODIXMOC_DEVICE, FuUsbDevice)
