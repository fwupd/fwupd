/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-steelseries-common.h"

FuSteelseriesDeviceKind
fu_steelseries_device_type_from_string(const gchar *name)
{
	if (g_strcmp0(name, "gamepad") == 0)
		return FU_STEELSERIES_DEVICE_GAMEPAD;
	if (g_strcmp0(name, "gamepad-dongle") == 0)
		return FU_STEELSERIES_DEVICE_GAMEPAD_DONGLE;
	return FU_STEELSERIES_DEVICE_UNKNOWN;
}

const gchar *
fu_steelseries_device_type_to_string(FuSteelseriesDeviceKind type)
{
	if (type == FU_STEELSERIES_DEVICE_GAMEPAD)
		return "gamepad";
	if (type == FU_STEELSERIES_DEVICE_GAMEPAD_DONGLE)
		return "gamepad-dongle";

	return "unknown";
}
