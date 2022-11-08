/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TI_TPS6598X_DEVICE (fu_ti_tps6598x_device_get_type())
G_DECLARE_FINAL_TYPE(FuTiTps6598xDevice, fu_ti_tps6598x_device, FU, TI_TPS6598X_DEVICE, FuUsbDevice)

GByteArray *
fu_ti_tps6598x_device_read_target_register(FuTiTps6598xDevice *self,
					   guint8 target,
					   guint8 addr,
					   guint8 length,
					   GError **error);
gboolean
fu_ti_tps6598x_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error);
