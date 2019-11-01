/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_UNIFYING_RUNTIME (fu_unifying_runtime_get_type ())
G_DECLARE_FINAL_TYPE (FuUnifyingRuntime, fu_unifying_runtime, FU, UNIFYING_RUNTIME, FuUdevDevice)
