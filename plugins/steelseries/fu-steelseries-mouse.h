/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_STEELSERIES_MOUSE (fu_steelseries_mouse_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesMouse, fu_steelseries_mouse, FU, STEELSERIES_MOUSE, FuUsbDevice)
