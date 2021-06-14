/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CPU_DEVICE (fu_cpu_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCpuDevice, fu_cpu_device, FU, CPU_DEVICE, FuDevice)

typedef enum {
	FU_CPU_DEVICE_FLAG_NONE		= 0,
	FU_CPU_DEVICE_FLAG_SHSTK	= 1 << 0,
	FU_CPU_DEVICE_FLAG_IBT		= 1 << 1,
	FU_CPU_DEVICE_FLAG_TME		= 1 << 2,
	FU_CPU_DEVICE_FLAG_SMAP		= 1 << 3,
} FuCpuDeviceFlag;

FuCpuDevice		*fu_cpu_device_new		(void);
gboolean		 fu_cpu_device_has_flag		(FuCpuDevice	*self,
							 FuCpuDeviceFlag flag);
