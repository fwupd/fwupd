/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"
#include "fu-progress.h"

#define FU_TYPE_DEVICE_PROGRESS (fu_device_progress_get_type())

G_DECLARE_FINAL_TYPE(FuDeviceProgress, fu_device_progress, FU, DEVICE_PROGRESS, GObject)

FuDeviceProgress *
fu_device_progress_new(FuDevice *device, FuProgress *progress) G_GNUC_WARN_UNUSED_RESULT;
