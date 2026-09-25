/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-focal-moc-device.h"

#define FU_TYPE_FOCAL_MOC_SIGNED_DEVICE (fu_focal_moc_signed_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocSignedDevice,
		     fu_focal_moc_signed_device,
		     FU,
		     FOCAL_MOC_SIGNED_DEVICE,
		     FuFocalMocDevice)
