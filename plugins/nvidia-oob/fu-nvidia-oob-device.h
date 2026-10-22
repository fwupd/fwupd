/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-nvidia-oob-bmc.h"

#define FU_TYPE_NVIDIA_OOB_DEVICE (fu_nvidia_oob_device_get_type())
G_DECLARE_FINAL_TYPE(FuNvidiaOobDevice, fu_nvidia_oob_device, FU, NVIDIA_OOB_DEVICE, FuDevice)

FuNvidiaOobDevice *
fu_nvidia_oob_device_new(FuContext *ctx, FuNvidiaOobBmc *bmc, FwupdJsonObject *json_member);
gboolean
fu_nvidia_oob_device_refresh_pending(FuNvidiaOobDevice *self, GError **error);
