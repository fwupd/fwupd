/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include "fu-cpu-device.h"

struct _FuCpuDevice {
	FuDevice		 parent_instance;
	gboolean		 has_shstk;
	gboolean		 has_ibt;
};

G_DEFINE_TYPE (FuCpuDevice, fu_cpu_device, FU_TYPE_DEVICE)

gboolean
fu_cpu_device_has_shstk (FuCpuDevice *self)
{
	return self->has_shstk;
}

gboolean
fu_cpu_device_has_ibt (FuCpuDevice *self)
{
	return self->has_ibt;
}

static void
fu_cpu_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuCpuDevice *self = FU_CPU_DEVICE (device);
	fu_common_string_append_kb (str, idt, "HasSHSTK", self->has_shstk);
	fu_common_string_append_kb (str, idt, "HasIBT", self->has_ibt);
}

static void
fu_cpu_device_parse_flags (FuCpuDevice *self, const gchar *data)
{
	g_auto(GStrv) flags = g_strsplit (data, " ", -1);
	for (guint i = 0; flags[i] != NULL; i++) {
		if (g_strcmp0 (flags[i], "shstk") == 0)
			self->has_shstk = TRUE;
		if (g_strcmp0 (flags[i], "ibt") == 0)
			self->has_ibt = TRUE;
	}
}

static void
fu_cpu_device_parse_section (FuDevice *dev, const gchar *data)
{
	g_auto(GStrv) lines = NULL;
	FuCpuDevice *self = FU_CPU_DEVICE (dev);

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
		} else if (g_str_has_prefix (lines[i], "flags")) {
			g_auto(GStrv) fields = g_strsplit (lines[i], ":", -1);
			if (fields[1] != NULL)
				fu_cpu_device_parse_flags (self, fields[1]);
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_cpu_device_to_string;
}

FuCpuDevice *
fu_cpu_device_new (const gchar *section)
{
	FuCpuDevice *device = NULL;
	device = g_object_new (FU_TYPE_CPU_DEVICE, NULL);
	fu_cpu_device_parse_section (FU_DEVICE (device), section);
	return device;
}
