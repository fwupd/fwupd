/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CPU_DEVICE (fu_cpu_device_get_type())
G_DECLARE_FINAL_TYPE(FuCpuDevice, fu_cpu_device, FU, CPU_DEVICE, FuDevice)

FuCpuDevice *
fu_cpu_device_new(FuContext *ctx);
