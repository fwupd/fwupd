/*
 * Copyright (C) 2023 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-amd-gpu-device.h"
#include "fu-amd-gpu-plugin.h"

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
}

static void
fu_amd_gpu_plugin_class_init(FuAmdGpuPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_amd_gpu_plugin_constructed;
}
