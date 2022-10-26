/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QSI_DOCK_MCU_DEVICE (fu_qsi_dock_mcu_device_get_type())
G_DECLARE_FINAL_TYPE(FuQsiDockMcuDevice,
		     fu_qsi_dock_mcu_device,
		     FU,
		     QSI_DOCK_MCU_DEVICE,
		     FuHidDevice)
gboolean
fu_qsi_dock_mcu_device_write_firmware_with_idx(FuQsiDockMcuDevice *self,
					       FuFirmware *firmware,
					       guint8 chip_idx,
					       FuProgress *progress,
					       FwupdInstallFlags flags,
					       GError **error);
