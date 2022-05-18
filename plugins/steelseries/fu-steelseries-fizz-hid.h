/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_STEELSERIES_FIZZ_HID (fu_steelseries_fizz_hid_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFizzHid,
		     fu_steelseries_fizz_hid,
		     FU,
		     STEELSERIES_FIZZ_HID,
		     FuHidDevice)
