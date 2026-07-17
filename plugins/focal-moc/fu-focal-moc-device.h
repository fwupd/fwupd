/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_MOC_DEVICE (fu_focal_moc_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocDevice,
		     fu_focal_moc_device,
		     FU,
		     FOCAL_MOC_DEVICE,
		     FuUsbDevice)
