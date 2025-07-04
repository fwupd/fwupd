/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELAN_KBD_RUNTIME (fu_elan_kbd_runtime_get_type())
G_DECLARE_FINAL_TYPE(FuElanKbdRuntime, fu_elan_kbd_runtime, FU, ELAN_KBD_RUNTIME, FuHidDevice)
