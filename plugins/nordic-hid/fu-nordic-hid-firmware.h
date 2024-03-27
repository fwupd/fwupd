/*
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_NORDIC_HID_FIRMWARE (fu_nordic_hid_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuNordicHidFirmware,
			 fu_nordic_hid_firmware,
			 FU,
			 NORDIC_HID_FIRMWARE,
			 FuFirmware)

struct _FuNordicHidFirmwareClass {
	FuFirmwareClass parent_class;
};
