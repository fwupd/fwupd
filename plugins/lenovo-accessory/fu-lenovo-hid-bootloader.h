/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_HID_BOOTLOADER (fu_lenovo_hid_bootloader_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoHidBootloader,
		     fu_lenovo_hid_bootloader,
		     FU,
		     LENOVO_HID_BOOTLOADER,
		     FuHidrawDevice)

#define FU_LENOVO_HID_BOOTLOADER_FLAG_IS_SIGNED "is-signed"
