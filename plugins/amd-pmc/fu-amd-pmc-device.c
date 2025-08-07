/*
 * Copyright 2022 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-pmc-device.h"
struct _FuAmdPmcDevice {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuAmdPmcDevice, fu_amd_pmc_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_amd_pmc_device_probe(FuDevice *device, GError **error)
{
	guint64 program = 0;
	g_autofree gchar *attr_smu_program = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *summary = NULL;

	version = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					    "smu_fw_version",
					    FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					    &error_local);
	if (version == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "unsupported kernel version");
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	attr_smu_program = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
						     "smu_program",
						     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						     error);
	if (attr_smu_program == NULL)
		return FALSE;
	if (!fu_strtoull(attr_smu_program, &program, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
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
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD);
	fu_device_set_vendor(FU_DEVICE(self), "AMD");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_physical_id(FU_DEVICE(self), "amd-pmc");
}

static void
fu_amd_pmc_device_class_init(FuAmdPmcDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_amd_pmc_device_probe;
}
