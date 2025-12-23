/*
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cpu-struct.h"
#include "fu-device.h"

#define FU_TYPE_CPU_DEVICE (fu_cpu_device_get_type())
G_DECLARE_FINAL_TYPE(FuCpuDevice, fu_cpu_device, FU, CPU_DEVICE, FuDevice)

FuCpuDevice *
fu_cpu_device_new(FuContext *ctx);

FuCpuFamily
fu_cpu_device_get_family(FuCpuDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_cpu_device_needs_mitigation(FuCpuDevice *self, FuCpuMitigationFlags mitigation_flag)
    G_GNUC_NON_NULL(1);
guint32
fu_cpu_device_get_sinkclose_microcode_ver(FuCpuDevice *self) G_GNUC_NON_NULL(1);
