/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-hidpp-runtime.h"

#define FU_TYPE_HIDPP_RUNTIME_BOLT (fu_logitech_hidpp_runtime_bolt_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechHidppRuntimeBolt,
		     fu_logitech_hidpp_runtime_bolt,
		     FU,
		     HIDPP_RUNTIME_BOLT,
		     FuLogitechHidppRuntime)
