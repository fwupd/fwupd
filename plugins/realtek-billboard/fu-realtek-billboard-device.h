/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_BILLBOARD_DEVICE (fu_realtek_billboard_device_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekBillboardDevice,
		     fu_realtek_billboard_device,
		     FU,
		     REALTEK_BILLBOARD_DEVICE,
		     FuUsbDevice)
