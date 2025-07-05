/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELAN_KBD_FIRMWARE (fu_elan_kbd_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuElanKbdFirmware, fu_elan_kbd_firmware, FU, ELAN_KBD_FIRMWARE, FuFirmware)

FuFirmware *
fu_elan_kbd_firmware_new(void);
