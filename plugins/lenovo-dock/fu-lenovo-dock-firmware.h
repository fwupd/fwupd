/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-lenovo-dock-struct.h"

#define FU_TYPE_LENOVO_DOCK_FIRMWARE (fu_lenovo_dock_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoDockFirmware,
		     fu_lenovo_dock_firmware,
		     FU,
		     LENOVO_DOCK_FIRMWARE,
		     FuFirmware)

#define FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE 0x100

guint16
fu_lenovo_dock_firmware_get_pid(FuLenovoDockFirmware *self);

FuStructLenovoDockUsage *
fu_lenovo_dock_firmware_get_usage(FuLenovoDockFirmware *self);
GPtrArray *
fu_lenovo_dock_firmware_get_usage_items(FuLenovoDockFirmware *self);
FuStructLenovoDockUsageItem *
fu_lenovo_dock_firmware_get_usage_item(FuLenovoDockFirmware *self,
				       FuLenovoDockComponentId component_id,
				       GError **error);
