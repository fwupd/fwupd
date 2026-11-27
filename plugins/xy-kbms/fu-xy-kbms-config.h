/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_XY_KBMS_CONFIG (fu_xy_kbms_config_get_type())
G_DECLARE_FINAL_TYPE(FuXyKbmsConfig, fu_xy_kbms_config, FU, XY_KBMS_CONFIG, FuDevice)

FuXyKbmsConfig *
fu_xy_kbms_config_new(FuDevice *proxy);
