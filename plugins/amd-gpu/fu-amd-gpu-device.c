/*
 * Copyright (C) 2023 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-amd-gpu-device.h"
struct _FuAmdGpuDevice {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuAmdGpuDevice, fu_amd_gpu_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_amd_gpu_device_probe(FuDevice *device, GError **error)
{
	const gchar *base;
	g_autofree gchar *rom = NULL;

	/* heuristic to detect !APU since we don't have sysfs file to indicate IS_APU */
	base = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	rom = g_build_filename(base, "rom", NULL);
	if (g_file_test(rom, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "only for APU");
		return FALSE;
	}
	fu_device_add_parent_guid(device, "cpu");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci", error);
}

static void
fu_amd_gpu_device_init(FuAmdGpuDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Graphics Processing Unit (GPU)");
}

static void
fu_amd_gpu_device_class_init(FuAmdGpuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_amd_gpu_device_probe;
}
