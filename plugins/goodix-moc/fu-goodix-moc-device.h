/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GOODIX_MOC_DEVICE (fu_goodix_moc_device_get_type())
G_DECLARE_FINAL_TYPE(FuGoodixMocDevice, fu_goodix_moc_device, FU, GOODIX_MOC_DEVICE, FuUsbDevice)
