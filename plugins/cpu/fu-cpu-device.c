/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include "fu-cpu-device.h"

struct _FuCpuDevice {
	FuDevice		 parent_instance;
};

G_DEFINE_TYPE (FuCpuDevice, fu_cpu_device, FU_TYPE_DEVICE)

static void fu_cpu_device_parse_section (FuDevice *dev, const gchar *data)
{
	g_auto(GStrv) lines = NULL;

	lines = g_strsplit (data, "\n", 0);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "vendor_id")) {
			g_auto(GStrv) fields = g_strsplit (lines[i], ":", -1);
			if (fields[1] != NULL)
				fu_device_set_vendor (dev, g_strchug (fields[1]));
		} else if (g_str_has_prefix (lines[i], "model name")) {
			g_auto(GStrv) fields = g_strsplit (lines[i], ":", -1);
			if (fields[1] != NULL)
				fu_device_set_name (dev, g_strchug (fields[1]));
		} else if (g_str_has_prefix (lines[i], "microcode")) {
			g_auto(GStrv) fields = g_strsplit (lines[i], ":", -1);
			if (fields[1] != NULL)
				fu_device_set_version (dev, g_strchug (fields[1]));
		} else if (g_str_has_prefix (lines[i], "physical id")) {
			g_auto(GStrv) fields = g_strsplit (lines[i], ":", -1);
			if (fields[1] != NULL) {
				g_autofree gchar *tmp = g_strdup_printf ("cpu:%s", g_strchug (fields[1]));
				fu_device_set_physical_id (dev, tmp);
			}
		}
	}
}

static void
fu_cpu_device_init (FuCpuDevice *self)
{
	fu_device_add_guid (FU_DEVICE (self), "cpu");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (FU_DEVICE (self), "computer");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_HEX);
}

static void
fu_cpu_device_class_init (FuCpuDeviceClass *klass)
{
}

FuCpuDevice *
fu_cpu_device_new (const gchar *section)
{
	FuCpuDevice *device = NULL;
	device = g_object_new (FU_TYPE_CPU_DEVICE, NULL);
	fu_cpu_device_parse_section (FU_DEVICE (device), section);
	return device;
}
