/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-nordic-hid-firmware.h"

#define FU_TYPE_NORDIC_HID_FIRMWARE_B0 (fu_nordic_hid_firmware_b0_get_type())
G_DECLARE_FINAL_TYPE(FuNordicHidFirmwareB0,
		     fu_nordic_hid_firmware_b0,
		     FU,
		     NORDIC_HID_FIRMWARE_B0,
		     FuNordicHidFirmware)
