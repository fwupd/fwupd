/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-fizz.h"

#define FU_TYPE_STEELSERIES_FIZZ_GEN2 (fu_steelseries_fizz_gen2_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFizzGen2,
		     fu_steelseries_fizz_gen2,
		     FU,
		     STEELSERIES_FIZZ_GEN2,
		     FuSteelseriesDevice)
