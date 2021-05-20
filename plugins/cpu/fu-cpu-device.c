/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include <sys/utsname.h>
#include <stdio.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "config.h"

#include <fwupdplugin.h>

#include "fu-cpu-device.h"

struct _FuCpuDevice {
	FuDevice		 parent_instance;
	FuCpuDeviceFlag		 flags;
	guint32			 family_id;
	guint32			 model_id;
	guint32			 stepping_id;
};

G_DEFINE_TYPE (FuCpuDevice, fu_cpu_device, FU_TYPE_DEVICE)

#define UCODE_BKP_EXTN	".bkp"

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

static gboolean
fu_cpu_is_updatable (const gchar *ucode_dir)
{
	g_autofree gchar *sysfs_ucode_reload = fu_common_get_path (FU_PATH_KIND_MICROCODE_RELOAD);

	/* This path may not be present on older kernels and won't be supported */
        if (g_access (sysfs_ucode_reload, R_OK|W_OK) < 0)
                return FALSE;

	if (ucode_dir != NULL) {
		if (g_access (ucode_dir, R_OK|W_OK) < 0)
			return FALSE;
	} else {
		 if (g_access ("/lib/firmware", R_OK|W_OK) < 0)
                        return FALSE;
	}

	/* check if /boot is writable for initramfs update */
	if (g_access ("/boot", R_OK|W_OK) < 0)
		return FALSE;

	return TRUE;
}

static void
fu_cpu_device_init (FuCpuDevice *self)
{
	fu_device_add_guid_full (FU_DEVICE (self), "cpu",
				 FU_DEVICE_INSTANCE_FLAG_NO_QUIRKS);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	if (fu_cpu_is_updatable (NULL)) {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_protocol (FU_DEVICE (self), "org.kernel.microcode-reload");
	}
	fu_device_add_icon (FU_DEVICE (self), "computer");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_physical_id (FU_DEVICE (self), "cpu:0");
}

static gboolean
fu_cpu_device_add_instance_ids (FuDevice *device, GError **error)
{
	FuCpuDevice *self = FU_CPU_DEVICE (device);
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

	self->family_id		= family_id;
	self->model_id		= model_id;
	self->stepping_id	= stepping_id;

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
fu_cpu_device_probe_manufacturer_id (FuDevice *device, GError **error)
{
	guint32 ebx = 0;
	guint32 ecx = 0;
	guint32 edx = 0;
	gchar str[13] = { '\0' };
	if (!fu_common_cpuid (0x0, NULL, &ebx, &ecx, &edx, error))
		return FALSE;
	if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), 0x0, /* dst */
			     (const guint8 *) &ebx, sizeof(ebx), 0x0, /* src */
			     sizeof(guint32), error))
		return FALSE;
	if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), 0x4, /* dst */
			     (const guint8 *) &edx, sizeof(edx), 0x0, /* src */
			     sizeof(guint32), error))
		return FALSE;
	if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), 0x8, /* dst */
			     (const guint8 *) &ecx, sizeof(ecx), 0x0, /* src */
			     sizeof(guint32), error))
		return FALSE;
	fu_device_set_vendor (device, fu_cpu_device_convert_vendor (str));
	if (fu_common_get_cpu_vendor () == FU_CPU_VENDOR_INTEL)
		fu_device_add_vendor_id (device, fu_cpu_device_convert_vendor ("CPU:INTEL"));
	return TRUE;
}

static gboolean
fu_cpu_device_probe_model (FuDevice *device, GError **error)
{
	guint32 eax = 0;
	guint32 ebx = 0;
	guint32 ecx = 0;
	guint32 edx = 0;
	gchar str[49] = { '\0' };

	for (guint32 i = 0; i < 3; i++) {
		if (!fu_common_cpuid (0x80000002 + i, &eax, &ebx, &ecx, &edx, error))
			return FALSE;
		if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), (16 * i) + 0x0, /* dst */
				     (const guint8 *) &eax, sizeof(eax), 0x0, /* src */
				     sizeof(guint32), error))
			return FALSE;
		if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), (16 * i) + 0x4, /* dst */
				     (const guint8 *) &ebx, sizeof(ebx), 0x0, /* src */
				     sizeof(guint32), error))
			return FALSE;
		if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), (16 * i) + 0x8, /* dst */
				     (const guint8 *) &ecx, sizeof(ecx), 0x0, /* src */
				     sizeof(guint32), error))
			return FALSE;
		if (!fu_memcpy_safe ((guint8 *) str, sizeof(str), (16 * i) + 0xc, /* dst */
				     (const guint8 *) &edx, sizeof(edx), 0x0, /* src */
				     sizeof(guint32), error))
			return FALSE;
	}
	fu_device_set_name (device, str);
	return TRUE;
}

static gboolean
fu_cpu_device_probe_extended_features (FuDevice *device, GError **error)
{
	FuCpuDevice *self = FU_CPU_DEVICE (device);
	guint32 ebx = 0;
	guint32 ecx = 0;

	if (!fu_common_cpuid (0x7, NULL, &ebx, &ecx, NULL, error))
		return FALSE;
	if ((ebx >> 20) & 0x1)
		self->flags |= FU_CPU_DEVICE_FLAG_SMAP;
	if ((ecx >> 7) & 0x1)
		self->flags |= FU_CPU_DEVICE_FLAG_SHSTK;
	if ((ecx >> 13) & 0x1)
		self->flags |= FU_CPU_DEVICE_FLAG_TME;
	if ((ecx >> 20) & 0x1)
		self->flags |= FU_CPU_DEVICE_FLAG_IBT;
	return TRUE;
}

static gboolean
fu_cpu_device_probe (FuDevice *device, GError **error)
{
	if (!fu_cpu_device_probe_manufacturer_id (device, error))
		return FALSE;
	if (!fu_cpu_device_probe_model (device, error))
		return FALSE;
	if (!fu_cpu_device_probe_extended_features (device, error))
		return FALSE;
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
	if (g_strcmp0 (key, "PciBcrAddr") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		fu_device_set_metadata_integer (device, "PciBcrAddr", tmp);
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "no supported");
	return FALSE;
}

static void
fu_cpu_device_add_security_attrs_intel_cet_enabled (FuCpuDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED);
	fwupd_security_attr_set_plugin (attr, fu_device_get_plugin (FU_DEVICE (self)));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);

	/* check for CET */
	if (!fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_SHSTK) ||
	    !fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_IBT)) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_cpu_device_add_security_attrs_intel_cet_active (FuCpuDevice *self, FuSecurityAttrs *attrs)
{
	gint exit_status = 0xff;
	g_autofree gchar *toolfn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* check for CET */
	if (!fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_SHSTK) ||
	    !fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_IBT))
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE);
	fwupd_security_attr_set_plugin (attr, fu_device_get_plugin (FU_DEVICE (self)));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attrs_append (attrs, attr);

	/* check that userspace has been compiled for CET support */
	toolfn = g_build_filename (FWUPD_LIBEXECDIR, "fwupd", "fwupd-detect-cet", NULL);
	if (!g_spawn_command_line_sync (toolfn, NULL, NULL, &exit_status, &error_local)) {
		g_warning ("failed to test CET: %s", error_local->message);
		return;
	}
	if (!g_spawn_check_exit_status (exit_status, &error_local)) {
		g_debug ("CET does not function, not supported: %s", error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_SUPPORTED);
}

static void
fu_cpu_device_add_security_attrs_intel_tme (FuCpuDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	fwupd_security_attr_set_plugin (attr, fu_device_get_plugin (FU_DEVICE (self)));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION);
	fu_security_attrs_append (attrs, attr);

	/* check for TME */
	if (!fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_TME)) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_cpu_device_add_security_attrs_intel_smap (FuCpuDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_SMAP);
	fwupd_security_attr_set_plugin (attr, fu_device_get_plugin (FU_DEVICE (self)));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION);
	fu_security_attrs_append (attrs, attr);

	/* check for SMEP and SMAP */
	if (!fu_cpu_device_has_flag (self, FU_CPU_DEVICE_FLAG_SMAP)) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_cpu_device_add_security_attrs (FuDevice *device, FuSecurityAttrs *attrs)
{
	FuCpuDevice *self = FU_CPU_DEVICE (device);

	/* only Intel */
	if (fu_common_get_cpu_vendor () != FU_CPU_VENDOR_INTEL)
		return;

	fu_cpu_device_add_security_attrs_intel_cet_enabled (self, attrs);
	fu_cpu_device_add_security_attrs_intel_cet_active (self, attrs);
	fu_cpu_device_add_security_attrs_intel_tme (self, attrs);
	fu_cpu_device_add_security_attrs_intel_smap (self, attrs);
}

static gboolean
fu_cpu_late_load_microcode (GError **error)
{
	char reload_char = '1';
	int fd;
	g_autofree gchar *sysfs_ucode_reload = fu_common_get_path (FU_PATH_KIND_MICROCODE_RELOAD);

	fd = open (sysfs_ucode_reload, O_WRONLY);
	if (fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open %s", sysfs_ucode_reload);
		return FALSE;
	}
	if (write (fd, &reload_char, 1) < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write to %s", sysfs_ucode_reload);
		close (fd);
		return FALSE;
	}

	close (fd);
	return TRUE;
}

static gboolean
fu_cpu_update_initrd_microcode (GError **error)
{
	struct utsname kernel_name;
	g_autoptr(GError) error_local = NULL;
	const gchar *argv[5] = {NULL};

	if (fu_common_find_program_in_path ("update-initramfs", &error_local) != NULL) {
		memset (&kernel_name, 0, sizeof(struct utsname));
		if (uname (&kernel_name) < 0) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "failed to read current kernel version");
			return FALSE;
		}
		argv[0] = "update-initramfs";
		argv[1] = "u";
		argv[2] = "-k";
		argv[3] =  kernel_name.release;
		argv[4] = NULL;

	} else if (fu_common_find_program_in_path ("dracut", &error_local) != NULL) {
		argv[0] = "dracut";
		argv[1] = "-f";
		argv[2]	= NULL;
	} else {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "couldn't find a tool to update the initial ramdisk with the new microcode");
		return FALSE;
	}

	if (!fu_common_spawn_sync (argv, NULL, NULL, 0, NULL, error)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to run initrd command: %s", argv[0]);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_cpu_restore_microcode (const gchar *ucode_path)
{
	g_autofree gchar *ucode_bkp_path = NULL;

	if (ucode_path == NULL)
		return FALSE;

	if (!g_remove (ucode_path))
		return FALSE;

	/* Now restore from backup if it exists */
	ucode_bkp_path = g_strdup_printf ("%s%s", ucode_path, UCODE_BKP_EXTN);

	if (ucode_bkp_path == NULL)
		return FALSE;

	if (!g_file_test (ucode_bkp_path, G_FILE_TEST_EXISTS))
		return TRUE;

	if (g_rename (ucode_bkp_path, ucode_path) < 0)
		return FALSE;

	return TRUE;
}

static void
fu_cpu_wr_fw_cleanup (FuDevice *device, const gchar *msg, GError **error)
{
	if (msg) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     msg);
	}
	fu_device_set_status (device, FWUPD_STATUS_IDLE);
}

static gboolean
fu_cpu_device_write_firmware (FuDevice *device, FuFirmware *firmware,
				FwupdInstallFlags flags, GError **error)
{
	FuCpuDevice *self = FU_CPU_DEVICE (device);
	g_autoptr(GOutputStream) ostream = NULL;
	g_autofree gchar *ucode_dir = NULL;
	g_autofree gchar *vendor = NULL;
	g_autofree gchar *ucode_bkp_path = NULL;
	g_autofree gchar *ucode_path = NULL;
	g_autoptr(GBytes) ucode_blob = NULL;
	GFile *ucode_fd;
	gssize size;

	/* how do we set fw version using MSR plugin ? */

	vendor = g_ascii_strdown (fu_device_get_vendor(device), -1);
	ucode_dir = g_strdup_printf ("/lib/firmware/%s-ucode", vendor);

	/* check device updatable */
	if (!fu_cpu_is_updatable (ucode_dir)) {
		fu_cpu_wr_fw_cleanup (device, "device not updatable", error);
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);

	if (firmware == NULL) {
		fu_cpu_wr_fw_cleanup (device, "firmware is NULL", error);
		return FALSE;
	}

	ucode_blob = fu_firmware_get_bytes (firmware, error);
	if (ucode_blob == NULL) {
		fu_cpu_wr_fw_cleanup (device, "firmware blob is NULL", error);
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);

	if (g_getenv ("FWUPD_CPU_VERBOSE") != NULL)
		fu_common_dump_bytes (G_LOG_DOMAIN, "microcode", ucode_blob);

	ucode_path = g_strdup_printf ("%s/%02x-%02x-%02x", ucode_dir,
					self->family_id, self->model_id,
					self->stepping_id);
	ucode_bkp_path = g_strdup_printf ("%s%s", ucode_path, UCODE_BKP_EXTN);

	if (ucode_path == NULL || ucode_bkp_path == NULL) {
		fu_cpu_wr_fw_cleanup (device, "failed to allocate memory", error);
		return FALSE;
	}

	/* only do update if we can make backup of existing microcode */
	if (g_file_test (ucode_path, G_FILE_TEST_EXISTS)) {
		if (g_rename (ucode_path, ucode_bkp_path) < 0) {
			fu_cpu_wr_fw_cleanup (device, "failed to create backup of existing microcode", error);
			return FALSE;
		}
	}

	ucode_fd = g_file_new_for_path (ucode_path);
	ostream = G_OUTPUT_STREAM (g_file_replace (ucode_fd, NULL, FALSE,
					G_FILE_CREATE_NONE, NULL, error));

	if (ostream == NULL) {
		fu_cpu_wr_fw_cleanup (device, "failed to create ucode file", error);
		/* restore previous microcode */
		if (!fu_cpu_restore_microcode (ucode_path))
			g_prefix_error (error, "failed to restore previous microcode: ");
		return FALSE;
	}

	size = g_output_stream_write_bytes (ostream, ucode_blob, NULL, error);
	if (!g_output_stream_close (ostream, NULL, error)) {
		fu_cpu_wr_fw_cleanup (device, "failed to close ostream", error);
		if (!fu_cpu_restore_microcode (ucode_path))
			g_prefix_error (error, "failed to restore previous microcode: ");
		return FALSE;
	}
	if (size < 0) {
		fu_cpu_wr_fw_cleanup (device, "failed to write ucode file", error);
		if (!fu_cpu_restore_microcode (ucode_path))
			g_prefix_error (error, "failed to restore previous microcode: ");
		return FALSE;
	}

	if (!fu_cpu_late_load_microcode (error)) {
		fu_cpu_wr_fw_cleanup (device, "microcode late load failed", error);
		if (!fu_cpu_restore_microcode (ucode_path))
			g_prefix_error (error, "failed to restore microcode after late load failure: ");
		return FALSE;
	}

	/* TODO again how to check the microcode version? check if updated and set it */

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_cpu_update_initrd_microcode (error)) {
		fu_cpu_wr_fw_cleanup (device, "microcode initrd update failed", error);
		if (!fu_cpu_restore_microcode (ucode_path))
			g_prefix_error (error, "failed to restore microcode after initrd update failure: ");
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

static void
fu_cpu_device_class_init (FuCpuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_cpu_device_to_string;
	klass_device->probe = fu_cpu_device_probe;
	klass_device->set_quirk_kv = fu_cpu_device_set_quirk_kv;
	klass_device->add_security_attrs = fu_cpu_device_add_security_attrs;
	klass_device->write_firmware = fu_cpu_device_write_firmware;
}

FuCpuDevice *
fu_cpu_device_new (FuContext *ctx)
{
	FuCpuDevice *device = NULL;
	device = g_object_new (FU_TYPE_CPU_DEVICE, "context", ctx, NULL);
	return device;
}
