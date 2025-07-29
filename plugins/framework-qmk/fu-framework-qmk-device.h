/*
 * Copyright 2025 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FRAMEWORK_QMK_DEVICE (fu_framework_qmk_device_get_type())
G_DECLARE_FINAL_TYPE(FuFrameworkQmkDevice,
		     fu_framework_qmk_device,
		     FU,
		     FRAMEWORK_QMK_DEVICE,
		     FuHidrawDevice)
