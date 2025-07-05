/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HUGHSKI_COLORHUG_DEVICE (fu_hughski_colorhug_device_get_type())
G_DECLARE_FINAL_TYPE(FuHughskiColorhugDevice,
		     fu_hughski_colorhug_device,
		     FU,
		     HUGHSKI_COLORHUG_DEVICE,
		     FuUsbDevice)
