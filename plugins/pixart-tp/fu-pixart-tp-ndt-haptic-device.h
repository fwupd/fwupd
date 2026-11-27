/*
 * Copyright 2026 Harris Tai <harris_tai@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PIXART_TP_NDT_HAPTIC_DEVICE (fu_pixart_tp_ndt_haptic_device_get_type())
G_DECLARE_FINAL_TYPE(FuPixartTpNdtHapticDevice,
		     fu_pixart_tp_ndt_haptic_device,
		     FU,
		     PIXART_TP_NDT_HAPTIC_DEVICE,
		     FuDevice)

FuPixartTpNdtHapticDevice *
fu_pixart_tp_ndt_haptic_device_new(FuDevice *proxy) G_GNUC_NON_NULL(1);
