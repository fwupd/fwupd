/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_USI_DOCK_MCU_DEVICE (fu_usi_dock_mcu_device_get_type())
G_DECLARE_FINAL_TYPE(FuUsiDockMcuDevice,
		     fu_usi_dock_mcu_device,
		     FU,
		     USI_DOCK_MCU_DEVICE,
		     FuHidDevice)
gboolean
fu_usi_dock_mcu_device_write_firmware_with_idx(FuUsiDockMcuDevice *self,
					       FuFirmware *firmware,
					       guint8 chip_idx,
					       FuProgress *progress,
					       FwupdInstallFlags flags,
					       GError **error);
