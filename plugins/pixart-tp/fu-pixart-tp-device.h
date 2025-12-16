/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-pixart-tp-struct.h"

#define FU_TYPE_PIXART_TP_DEVICE (fu_pixart_tp_device_get_type())
G_DECLARE_FINAL_TYPE(FuPixartTpDevice, fu_pixart_tp_device, FU, PIXART_TP_DEVICE, FuHidrawDevice)

gboolean
fu_pixart_tp_device_register_user_write(FuPixartTpDevice *self,
					FuPixartTpUserBank bank,
					guint8 addr,
					guint8 val,
					GError **error) G_GNUC_NON_NULL(1);
