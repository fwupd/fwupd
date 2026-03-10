/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_DOCK_FIRMWARE (fu_lenovo_dock_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoDockFirmware,
		     fu_lenovo_dock_firmware,
		     FU,
		     LENOVO_DOCK_FIRMWARE,
		     FuFirmware)

#define FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE 256

FuFirmware *
fu_lenovo_dock_firmware_new(void);
guint16
fu_lenovo_dock_firmware_get_pid(FuLenovoDockFirmware *self);
