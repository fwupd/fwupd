/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELAN_KBD_DEBUG_DEVICE (fu_elan_kbd_debug_device_get_type())
G_DECLARE_FINAL_TYPE(FuElanKbdDebugDevice,
		     fu_elan_kbd_debug_device,
		     FU,
		     ELAN_KBD_DEBUG_DEVICE,
		     FuUsbDevice)
