/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCALTECH_MOC_DEVICE (fu_focaltech_moc_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocaltechMocDevice,
		     fu_focaltech_moc_device,
		     FU,
		     FOCALTECH_MOC_DEVICE,
		     FuUsbDevice)
