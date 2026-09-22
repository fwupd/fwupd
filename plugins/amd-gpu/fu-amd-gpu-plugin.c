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
#include "fu-amd-gpu-pldm-device.h"
#include "fu-amd-gpu-pldm-firmware.h"
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
fu_amd_gpu_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	g_autoptr(FuDevice) pldm_device = NULL;
	g_autoptr(GError) error_local = NULL;

	/* only wrap amd-gpu devices that expose the remote-management interface */
	if (!FU_IS_AMDGPU_DEVICE(device))
		return;
	if (!fu_device_has_private_flag(device, FU_AMD_GPU_DEVICE_FLAG_REMOTE_MGMT))
		return;

	/* already wrapped */
	if (fu_device_get_parent(device, NULL) != NULL)
		return;

	/* create the remote-management device as the parent of the GPU */
	pldm_device = fu_amd_gpu_pldm_device_new(device);
	if (!fu_device_setup(pldm_device, &error_local)) {
		g_warning("failed to set up PLDM remote-management device: %s",
			  error_local->message);
		return;
	}
	fu_device_add_child(pldm_device, device);
	fu_plugin_add_device(plugin, pldm_device);
}

static void
fu_amd_gpu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMDGPU_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_AMD_GPU_PLDM_DEVICE);
	/* navi3x and later use PSP firmware container */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_AMD_GPU_PSP_FIRMWARE);
	/* navi 2x and older have the ATOM firmware at start of image */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_AMD_GPU_ATOM_FIRMWARE);
	/* PLDM firmware update package (DMTF DSP0267) */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_AMD_GPU_PLDM_FIRMWARE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_amd_gpu_plugin_parent_class)->constructed(obj);
}

static void
fu_amd_gpu_plugin_class_init(FuAmdGpuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_amd_gpu_plugin_constructed;
	plugin_class->device_registered = fu_amd_gpu_plugin_device_registered;
}
