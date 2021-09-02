/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef enum {
	FU_STEELSERIES_DEVICE_UNKNOWN = 0,
	FU_STEELSERIES_DEVICE_GAMEPAD,
	FU_STEELSERIES_DEVICE_GAMEPAD_DONGLE
} FuSteelseriesDeviceKind;

FuSteelseriesDeviceKind
fu_steelseries_device_type_from_string(const gchar *name);

const gchar *
fu_steelseries_device_type_to_string(FuSteelseriesDeviceKind type);
