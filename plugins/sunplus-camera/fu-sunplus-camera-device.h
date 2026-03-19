/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SUNPLUS_CAMERA_DEVICE (fu_sunplus_camera_device_get_type())
G_DECLARE_FINAL_TYPE(FuSunplusCameraDevice,
		     fu_sunplus_camera_device,
		     FU,
		     SUNPLUS_CAMERA_DEVICE,
		     FuV4lDevice)
