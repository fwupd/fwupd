/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-device.h"

#define FU_TYPE_STEELSERIES_GAMEPAD (fu_steelseries_gamepad_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesGamepad,
		     fu_steelseries_gamepad,
		     FU,
		     STEELSERIES_GAMEPAD,
		     FuSteelseriesDevice)
