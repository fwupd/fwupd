/*
 * Copyright 2023 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-gpu-atom-firmware.h"
#include "fu-amd-gpu-device.h"
#include "fu-amd-gpu-plugin.h"
#include "fu-amd-gpu-psp-firmware.h"

struct _FuAmdGpuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAmdGpuPlugin, fu_amd_gpu_plugin, FU_TYPE_PLUGIN)

static void
fu_amd_gpu_plugin_init(FuAmdGpuPlugin *self)
{
}

static void
fu_amd_gpu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMDGPU_DEVICE);
	/* Navi3x and later use PSP firmware container */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_AMD_GPU_PSP_FIRMWARE);
	/* Navi 2x and older have the ATOM firmware at start of image */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_AMD_GPU_ATOM_FIRMWARE);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "optionrom");
}

static void
fu_amd_gpu_plugin_class_init(FuAmdGpuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_amd_gpu_plugin_constructed;
}
