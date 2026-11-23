/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_RTS54HID_MODULE (fu_realtek_rts54hid_module_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekRts54hidModule,
		     fu_realtek_rts54hid_module,
		     FU,
		     REALTEK_RTS54HID_MODULE,
		     FuDevice)
