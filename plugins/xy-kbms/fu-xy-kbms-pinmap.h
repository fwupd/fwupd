/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_XY_KBMS_PINMAP (fu_xy_kbms_pinmap_get_type())
G_DECLARE_FINAL_TYPE(FuXyKbmsPinmap, fu_xy_kbms_pinmap, FU, XY_KBMS_PINMAP, FuFirmware)

FuFirmware *
fu_xy_kbms_pinmap_new(void);
