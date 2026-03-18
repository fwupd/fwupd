/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LXSTOUCH_FIRMWARE (fu_lxstouch_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLxstouchFirmware, fu_lxstouch_firmware, FU, LXSTOUCH_FIRMWARE, FuFirmware)

FuFirmware *
fu_lxstouch_firmware_new(void);
guint32
fu_lxstouch_firmware_get_offset(FuLxstouchFirmware *self);
