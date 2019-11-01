/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_UNIFYING_RUNTIME (fu_logitech_hidpp_runtime_get_type ())
G_DECLARE_FINAL_TYPE (FuLogitechHidPpRuntime, fu_logitech_hidpp_runtime, FU, UNIFYING_RUNTIME, FuUdevDevice)
