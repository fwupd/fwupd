/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_PURE_HID_DEVICE (fu_ccgx_pure_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuCcgxPureHidDevice,
		     fu_ccgx_pure_hid_device,
		     FU,
		     CCGX_PURE_HID_DEVICE,
		     FuHidDevice)

typedef enum {
	CCGX_HID_INFO_E0 = 0xE0,
	CCGX_HID_COMMAND_E1 = 0xE1,
	CCGX_HID_WRITE_E2 = 0xE2,
	CCGX_HID_READ_E3 = 0xE3,
	CCGX_HID_CUSTOM = 0xE4,
} CcgHidReport;

typedef enum {
	CCGX_HID_CMD_JUMP = 0x01,
	CCGX_HID_CMD_FLASH = 0x02,
	CCGX_HID_CMD_MODE = 0x06,
} CcgHidCommand;
