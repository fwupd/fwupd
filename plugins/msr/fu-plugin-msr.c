/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <cpuid.h>

typedef union {
	guint32 data;
	struct {
		guint32 enabled : 1;
		guint32 rsrvd : 29;
		guint32 locked : 1;
		guint32 debug_occurred : 1;
	} __attribute__((packed)) fields;
} FuMsrIa32Debug;

typedef union {
	guint32 data;
	struct {
		guint32 unknown0 : 23; /* 0 -> 22 inc */
		guint32 sme_is_enabled : 1;
		guint32 unknown1 : 8;
	} __attribute__((packed)) fields;
} FuMsrAMD64Syscfg;

typedef union {
	guint32 data;
	struct {
		guint32 sev_is_enabled : 1;
		guint32 unknown0 : 31;
	} __attribute__((packed)) fields;
} FuMsrAMD64Sev;

struct FuPluginData {
	gboolean ia32_debug_supported;
	FuMsrIa32Debug ia32_debug;
	gboolean amd64_syscfg_supported;
	gboolean amd64_sev_supported;
	FuMsrAMD64Syscfg amd64_syscfg;
	FuMsrAMD64Sev amd64_sev;
};

#define PCI_MSR_IA32_DEBUG_INTERFACE 0xc80
#define PCI_MSR_IA32_BIOS_SIGN_ID    0x8b
#define PCI_MSR_AMD64_SYSCFG	     0xC0010010
#define PCI_MSR_AMD64_SEV	     0xC0010131

static void
fu_plugin_msr_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
	fu_plugin_add_udev_subsystem(plugin, "msr");
}

static gboolean
fu_plugin_msr_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	guint eax = 0;
	guint ebx = 0;
	guint ecx = 0;

	if (!g_file_test("/dev/cpu", G_FILE_TEST_IS_DIR)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "missing kernel support");
		return FALSE;
	}

	/* sdbg is supported: https://en.wikipedia.org/wiki/CPUID */
	if (fu_common_get_cpu_vendor() == FU_CPU_VENDOR_INTEL) {
		if (!fu_common_cpuid(0x01, NULL, NULL, &ecx, NULL, error))
			return FALSE;
		priv->ia32_debug_supported = ((ecx >> 11) & 0x1) > 0;
	}

	/* indicates support for SME and SEV */
	if (fu_common_get_cpu_vendor() == FU_CPU_VENDOR_AMD) {
		if (!fu_common_cpuid(0x8000001f, &eax, &ebx, NULL, NULL, error))
			return FALSE;
		g_debug("SME/SEV check MSR: eax 0%x, ebx 0%x", eax, ebx);
		priv->amd64_syscfg_supported = ((eax >> 0) & 0x1) > 0;
		priv->amd64_sev_supported = ((eax >> 1) & 0x1) > 0;
	}

	return TRUE;
}

static gboolean
fu_plugin_msr_backend_device_added(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDevice *device_cpu = fu_plugin_cache_lookup(plugin, "cpu");
	FuPluginData *priv = fu_plugin_get_data(plugin);
	guint8 buf[8] = {0x0};
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree gchar *basename = NULL;

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "msr") != 0)
		return TRUE;

	/* we only care about the first processor */
	basename = g_path_get_basename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
	if (g_strcmp0(basename, "msr0") != 0)
		return TRUE;

	/* open the config */
	fu_device_set_physical_id(FU_DEVICE(device), "msr");
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* grab Intel MSR */
	if (priv->ia32_debug_supported) {
		if (!fu_udev_device_pread_full(FU_UDEV_DEVICE(device),
					       PCI_MSR_IA32_DEBUG_INTERFACE,
					       buf,
					       sizeof(buf),
					       error)) {
			g_prefix_error(error, "could not read IA32_DEBUG_INTERFACE: ");
			return FALSE;
		}
		if (!fu_common_read_uint32_safe(buf,
						sizeof(buf),
						0x0,
						&priv->ia32_debug.data,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		g_debug("IA32_DEBUG_INTERFACE: enabled=%i, locked=%i, debug_occurred=%i",
			priv->ia32_debug.fields.enabled,
			priv->ia32_debug.fields.locked,
			priv->ia32_debug.fields.debug_occurred);
	}

	/* grab AMD MSRs */
	if (priv->amd64_syscfg_supported) {
		if (!fu_udev_device_pread_full(FU_UDEV_DEVICE(device),
					       PCI_MSR_AMD64_SYSCFG,
					       buf,
					       sizeof(buf),
					       error)) {
			g_prefix_error(error, "could not read PCI_MSR_AMD64_SYSCFG: ");
			return FALSE;
		}
		if (!fu_common_read_uint32_safe(buf,
						sizeof(buf),
						0x0,
						&priv->amd64_syscfg.data,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		g_debug("PCI_MSR_AMD64_SYSCFG: 0%x, sme_is_enabled=%i",
			priv->amd64_syscfg.data,
			priv->amd64_syscfg.fields.sme_is_enabled);
	}
	if (priv->amd64_sev_supported) {
		if (!fu_udev_device_pread_full(FU_UDEV_DEVICE(device),
					       PCI_MSR_AMD64_SEV,
					       buf,
					       sizeof(buf),
					       error)) {
			g_prefix_error(error, "could not read PCI_MSR_AMD64_SEV: ");
			return FALSE;
		}
		if (!fu_common_read_uint32_safe(buf,
						sizeof(buf),
						0x0,
						&priv->amd64_sev.data,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		g_debug("PCI_MSR_AMD64_SEV: 0%x, sev_is_enabled=%i",
			priv->amd64_sev.data,
			priv->amd64_sev.fields.sev_is_enabled);
	}

	/* get microcode version */
	if (device_cpu != NULL) {
		guint32 ver_raw;
		if (!fu_udev_device_pread_full(FU_UDEV_DEVICE(device),
					       PCI_MSR_IA32_BIOS_SIGN_ID,
					       buf,
					       sizeof(buf),
					       error)) {
			g_prefix_error(error, "could not read IA32_BIOS_SIGN_ID: ");
			return FALSE;
		}
		fu_common_dump_raw(G_LOG_DOMAIN, "IA32_BIOS_SIGN_ID", buf, sizeof(buf));
		if (!fu_common_read_uint32_safe(buf,
						sizeof(buf),
						0x4,
						&ver_raw,
						G_LITTLE_ENDIAN,
						error))
			return FALSE;
		if (ver_raw != 0) {
			FwupdVersionFormat verfmt = fu_device_get_version_format(device_cpu);
			g_autofree gchar *ver_str = NULL;
			ver_str = fu_common_version_from_uint32(ver_raw, verfmt);
			g_debug("setting microcode version to %s", ver_str);
			fu_device_set_version(device_cpu, ver_str);
			fu_device_set_version_raw(device_cpu, ver_raw);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_msr_device_registered(FuPlugin *plugin, FuDevice *dev)
{
	if (g_strcmp0(fu_device_get_plugin(dev), "cpu") == 0) {
		fu_plugin_cache_add(plugin, "cpu", dev);
		return;
	}
}

static void
fu_plugin_add_security_attr_dci_enabled(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	FuDevice *device = fu_plugin_cache_lookup(plugin, "cpu");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (fu_common_get_cpu_vendor() != FU_CPU_VENDOR_INTEL)
		return;
	if (!priv->ia32_debug_supported)
		return;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	if (device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(device));
	fu_security_attrs_append(attrs, attr);

	/* check fields */
	if (priv->ia32_debug.fields.enabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_plugin_add_security_attr_dci_locked(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	FuDevice *device = fu_plugin_cache_lookup(plugin, "cpu");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (fu_common_get_cpu_vendor() != FU_CPU_VENDOR_INTEL)
		return;
	if (!priv->ia32_debug_supported)
		return;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	if (device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(device));
	fu_security_attrs_append(attrs, attr);

	/* check fields */
	if (!priv->ia32_debug.fields.locked) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static gboolean
fu_plugin_msr_safe_kernel_for_sme(FuPlugin *plugin, GError **error)
{
	g_autofree gchar *min = fu_plugin_get_config_value(plugin, "MinimumSmeKernelVersion");

	if (min == NULL) {
		g_debug("Ignoring kernel safety checks");
		return TRUE;
	}
	return fu_common_check_kernel_version(min, error);
}

static gboolean
fu_plugin_msr_kernel_enabled_sme(GError **error)
{
	g_autofree gchar *buf = NULL;
	gsize bufsz = 0;
	if (!g_file_get_contents("/proc/cpuinfo", &buf, &bufsz, error))
		return FALSE;
	if (bufsz > 0) {
		g_auto(GStrv) tokens = fu_common_strnsplit(buf, bufsz, " ", -1);
		for (guint i = 0; tokens[i] != NULL; i++) {
			if (g_strcmp0(tokens[i], "sme") == 0)
				return TRUE;
		}
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sme support not enabled by kernel");
	return FALSE;
}

static void
fu_plugin_add_security_attr_amd_sme_enabled(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	FuDevice *device = fu_plugin_cache_lookup(plugin, "cpu");
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* this MSR is only valid for a subset of AMD CPUs */
	if (fu_common_get_cpu_vendor() != FU_CPU_VENDOR_AMD)
		return;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION);
	if (device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(device));
	fu_security_attrs_append(attrs, attr);

	/* check fields */
	if (!priv->amd64_syscfg_supported) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	if (!priv->amd64_syscfg.fields.sme_is_enabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	if (!fu_plugin_msr_safe_kernel_for_sme(plugin, &error_local)) {
		g_debug("Unable to properly detect SME: %s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
		return;
	}

	if (!(fu_plugin_msr_kernel_enabled_sme(&error_local))) {
		g_debug("%s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);
	fwupd_security_attr_add_obsolete(attr, "pci_psp");
}

static void
fu_plugin_msr_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	fu_plugin_add_security_attr_dci_enabled(plugin, attrs);
	fu_plugin_add_security_attr_dci_locked(plugin, attrs);
	fu_plugin_add_security_attr_amd_sme_enabled(plugin, attrs);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_msr_init;
	vfuncs->startup = fu_plugin_msr_startup;
	vfuncs->backend_device_added = fu_plugin_msr_backend_device_added;
	vfuncs->add_security_attrs = fu_plugin_msr_add_security_attrs;
	vfuncs->device_registered = fu_plugin_msr_device_registered;
}
