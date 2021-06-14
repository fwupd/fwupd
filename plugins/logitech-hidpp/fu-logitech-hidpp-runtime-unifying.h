/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-hidpp-runtime.h"

#define FU_TYPE_HIDPP_RUNTIME_UNIFYING (fu_logitech_hidpp_runtime_unifying_get_type ())
G_DECLARE_FINAL_TYPE (FuLogitechHidPpRuntimeUnifying, fu_logitech_hidpp_runtime_unifying, FU, HIDPP_RUNTIME_UNIFYING, FuLogitechHidPpRuntime)
