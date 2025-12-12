/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PXI_TP_HAPTIC_DEVICE (fu_pxi_tp_haptic_device_get_type())

G_DECLARE_FINAL_TYPE(FuPxiTpHapticDevice,
		     fu_pxi_tp_haptic_device,
		     FU,
		     PXI_TP_HAPTIC_DEVICE,
		     FuDevice)

FuPxiTpHapticDevice *
fu_pxi_tp_haptic_device_new(FuDevice *parent);
