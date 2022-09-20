/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-amd-pmc-device.h"
struct _FuAmdPmcDevice {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuAmdPmcDevice, fu_amd_pmc_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_amd_pmc_device_probe(FuDevice *device, GError **error)
{
	guint64 program;
	const gchar *version;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuUdevDevice) udev_parent = NULL;
	g_autofree gchar *summary = NULL;

	version =
	    fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "smu_fw_version", &error_local);
	if (version == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "unsupported kernel version");
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	if (!fu_udev_device_get_sysfs_attr_uint64(FU_UDEV_DEVICE(device),
						  "smu_program",
						  &program,
						  error))
		return FALSE;

	fu_device_set_version(device, version);
	summary = g_strdup_printf("Microcontroller used within CPU/APU program %" G_GUINT64_FORMAT,
				  program);
	fu_device_set_summary(device, summary);
	fu_device_add_instance_id(device, fu_device_get_backend_id(device));

	return TRUE;
}

static void
fu_amd_pmc_device_init(FuAmdPmcDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "System Management Unit (SMU)");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_add_parent_guid(FU_DEVICE(self), "cpu");
	fu_device_set_vendor(FU_DEVICE(self), "AMD");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_physical_id(FU_DEVICE(self), "amd-pmc");
}

static void
fu_amd_pmc_device_class_init(FuAmdPmcDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_amd_pmc_device_probe;
}
