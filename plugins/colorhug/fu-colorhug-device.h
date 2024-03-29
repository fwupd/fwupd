/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_COLORHUG_DEVICE (fu_colorhug_device_get_type())
G_DECLARE_FINAL_TYPE(FuColorhugDevice, fu_colorhug_device, FU, COLORHUG_DEVICE, FuUsbDevice)
