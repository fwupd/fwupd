/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-device.h"

#define FU_TYPE_STEELSERIES_FIZZ (fu_steelseries_fizz_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFizz,
		     fu_steelseries_fizz,
		     FU,
		     STEELSERIES_FIZZ,
		     FuSteelseriesDevice)
