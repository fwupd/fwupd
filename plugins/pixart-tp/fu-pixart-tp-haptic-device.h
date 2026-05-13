/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PIXART_TP_HAPTIC_DEVICE (fu_pixart_tp_haptic_device_get_type())
G_DECLARE_FINAL_TYPE(FuPixartTpHapticDevice,
		     fu_pixart_tp_haptic_device,
		     FU,
		     PIXART_TP_HAPTIC_DEVICE,
		     FuDevice)

FuPixartTpHapticDevice *
fu_pixart_tp_haptic_device_new(FuDevice *proxy) G_GNUC_NON_NULL(1);
