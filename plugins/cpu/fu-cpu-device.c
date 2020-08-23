/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include "fu-cpu-device.h"

struct _FuCpuDevice {
	FuDevice		 parent_instance;
	FuCpuDeviceFlag		 flags;
};

G_DEFINE_TYPE (FuCpuDevice, fu_cpu_device, FU_TYPE_DEVICE)

gboolean
fu_cpu_device_has_flag (FuCpuDevice *self, FuCpuDeviceFlag flag)
{
	return (self->flags & flag) > 0;
}

static void
fu_cpu_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuCpuDevice *self = FU_CPU_DEVICE (device);
	fu_common_string_append_kb (str, idt, "HasSHSTK",
				    fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_SHSTK));
	fu_common_string_append_kb (str, idt, "HasIBT",
				    fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_IBT));
	fu_common_string_append_kb (str, idt, "HasTME",
				    fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_TME));
	fu_common_string_append_kb (str, idt, "HasSMAP",
				    fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_SMAP));
}

static const gchar *
fu_cpu_device_convert_vendor (const gchar *vendor)
{
	if (g_strcmp0 (vendor, "GenuineIntel") == 0)
		return "Intel";
	if (g_strcmp0 (vendor, "AuthenticAMD") == 0 ||
	    g_strcmp0 (vendor, "AMDisbetter!") == 0)
		return "AMD";
	if (g_strcmp0 (vendor, "CentaurHauls") == 0)
		return "IDT";
	if (g_strcmp0 (vendor, "CyrixInstead") == 0)
		return "Cyrix";
	if (g_strcmp0 (vendor, "TransmetaCPU") == 0 ||
	    g_strcmp0 (vendor, "GenuineTMx86") == 0)
		return "Transmeta";
	if (g_strcmp0 (vendor, "Geode by NSC") == 0)
		return "National Semiconductor";
	if (g_strcmp0 (vendor, "NexGenDriven") == 0)
		return "NexGen";
	if (g_strcmp0 (vendor, "RiseRiseRise") == 0)
		return "Rise";
	if (g_strcmp0 (vendor, "SiS SiS SiS ") == 0)
		return "SiS";
	if (g_strcmp0 (vendor, "UMC UMC UMC ") == 0)
		return "UMC";
	if (g_strcmp0 (vendor, "VIA VIA VIA ") == 0)
		return "VIA";
	if (g_strcmp0 (vendor, "Vortex86 SoC") == 0)
		return "Vortex";
	if (g_strcmp0 (vendor, " Shanghai ") == 0)
		return "Zhaoxin";
	if (g_strcmp0 (vendor, "HygonGenuine") == 0)
		return "Hygon";
	if (g_strcmp0 (vendor, "E2K MACHINE") == 0)
		return "MCST";
	if (g_strcmp0 (vendor, "bhyve bhyve ") == 0)
		return "bhyve";
	if (g_strcmp0 (vendor, " KVMKVMKVM ") == 0)
		return "KVM";
	if (g_strcmp0 (vendor, "TCGTCGTCGTCG") == 0)
		return "QEMU";
	if (g_strcmp0 (vendor, "Microsoft Hv") == 0)
		return "Microsoft";
	if (g_strcmp0 (vendor, " lrpepyh vr") == 0)
		return "Parallels";
	if (g_strcmp0 (vendor, "VMwareVMware") == 0)
		return "VMware";
	if (g_strcmp0 (vendor, "XenVMMXenVMM") == 0)
		return "Xen";
	if (g_strcmp0 (vendor, "ACRNACRNACRN") == 0)
		return "ACRN";
	if (g_strcmp0 (vendor, " QNXQVMBSQG ") == 0)
		return "QNX";
	if (g_strcmp0 (vendor, "VirtualApple") == 0)
		return "Apple";
	return vendor;
}

static void
fu_cpu_device_parse_flags (FuCpuDevice *self, const gchar *data)
{
	g_auto(GStrv) flags = g_strsplit (data, " ", -1);
	for (guint i = 0; flags[i] != NULL; i++) {
		if (g_strcmp0 (flags[i], "shstk") == 0)
			self->flags |= FU_CPU_DEVICE_FLAG_SHSTK;
		if (g_strcmp0 (flags[i], "ibt") == 0)
			self->flags |= FU_CPU_DEVICE_FLAG_IBT;
		if (g_strcmp0 (flags[i], "tme") == 0)
			self->flags |= FU_CPU_DEVICE_FLAG_TME;
		if (g_strcmp0 (flags[i], "smap") == 0)
			self->flags |= FU_CPU_DEVICE_FLAG_SMAP;
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
				fu_device_set_vendor (dev, fu_cpu_device_convert_vendor (fields[1] + 1));
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

static gboolean
fu_cpu_device_add_instance_ids (FuDevice *device, GError **error)
{
	guint32 eax = 0;
	guint32 family_id;
	guint32 family_id_ext;
	guint32 model_id;
	guint32 model_id_ext;
	guint32 processor_id;
	guint32 stepping_id;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *devid3 = NULL;

	/* decode according to https://en.wikipedia.org/wiki/CPUID */
	if (!fu_common_cpuid (0x1, &eax, NULL, NULL, NULL, error))
		return FALSE;
	processor_id = (eax >> 12) & 0x3;
	model_id = (eax >> 4) & 0xf;
	family_id = (eax >> 8) & 0xf;
	model_id_ext = (eax >> 16) & 0xf;
	family_id_ext = (eax >> 20) & 0xff;
	stepping_id = eax & 0xf;

	/* use extended IDs where required */
	if (family_id == 6 || family_id == 15)
		model_id |= model_id_ext << 4;
	if (family_id == 15)
		family_id += family_id_ext;

	devid1 = g_strdup_printf ("CPUID\\PRO_%01X&FAM_%02X",
				  processor_id,
				  family_id);
	fu_device_add_instance_id (device, devid1);
	devid2 = g_strdup_printf ("CPUID\\PRO_%01X&FAM_%02X&MOD_%02X",
				  processor_id,
				  family_id,
				  model_id);
	fu_device_add_instance_id (device, devid2);
	devid3 = g_strdup_printf ("CPUID\\PRO_%01X&FAM_%02X&MOD_%02X&STP_%01X",
				  processor_id,
				  family_id,
				  model_id,
				  stepping_id);
	fu_device_add_instance_id (device, devid3);
	return TRUE;
}

static gboolean
fu_cpu_device_probe (FuDevice *device, GError **error)
{
	if (!fu_cpu_device_add_instance_ids (device, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_cpu_device_set_quirk_kv (FuDevice *device,
			    const gchar *key,
			    const gchar *value,
			    GError **error)
{
	if (g_strcmp0 (key, "BcrAddr") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		fu_device_set_metadata_integer (device, "BcrAddr", tmp);
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "no supported");
	return FALSE;
}

static void
fu_cpu_device_class_init (FuCpuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_cpu_device_to_string;
	klass_device->probe = fu_cpu_device_probe;
	klass_device->set_quirk_kv = fu_cpu_device_set_quirk_kv;
}

FuCpuDevice *
fu_cpu_device_new (const gchar *section)
{
	FuCpuDevice *device = NULL;
	device = g_object_new (FU_TYPE_CPU_DEVICE, NULL);
	fu_cpu_device_parse_section (FU_DEVICE (device), section);
	return device;
}
