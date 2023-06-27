/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <cpuid.h>

#include "fu-msr-plugin.h"

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
	guint64 data;
	struct {
		guint32 lock_ro : 1;
		guint32 enable : 1;
		guint32 key_select : 1;
		guint32 save_key_for_standby : 1;
		guint32 policy_encryption_algo : 4;
		guint32 reserved1 : 23;
		guint32 bypass_enable : 1;
		guint32 mk_tme_keyid_bits : 4;
		guint32 reserved2 : 12;
		guint32 mk_tme_crypto_algs : 16;
	} __attribute__((packed)) fields;
} FuMsrIa32TmeActivation;

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

struct _FuMsrPlugin {
	FuPlugin parent_instance;
	gboolean ia32_debug_supported;
	gboolean ia32_tme_supported;
	FuMsrIa32Debug ia32_debug;
	FuMsrIa32TmeActivation ia32_tme_activation;
	gboolean amd64_syscfg_supported;
	gboolean amd64_sev_supported;
	FuMsrAMD64Syscfg amd64_syscfg;
	FuMsrAMD64Sev amd64_sev;
};

G_DEFINE_TYPE(FuMsrPlugin, fu_msr_plugin, FU_TYPE_PLUGIN)

#define PCI_MSR_IA32_DEBUG_INTERFACE 0xc80
#define PCI_MSR_IA32_TME_ACTIVATION  0x982
#define PCI_MSR_IA32_BIOS_SIGN_ID    0x8b
#define PCI_MSR_AMD64_SYSCFG	     0xC0010010
#define PCI_MSR_AMD64_SEV	     0xC0010131

/* defaults changed here will also be reflected in the fwupd.conf man page */
#define FU_MSR_CONFIG_DEFAULT_MINIMUM_SME_KERNEL_VERSION "5.18.0"

static void
fu_msr_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
	if (self->ia32_debug_supported) {
		fu_string_append_kb(str,
				    idt,
				    "Ia32DebugInterfaceEnabled",
				    self->ia32_debug.fields.enabled);
		fu_string_append_kb(str,
				    idt,
				    "Ia32DebugInterfaceLocked",
				    self->ia32_debug.fields.locked);
		fu_string_append_kb(str,
				    idt,
				    "Ia32DebugInterfaceDebugOccurred",
				    self->ia32_debug.fields.debug_occurred);
	}
	if (self->ia32_tme_supported) {
		fu_string_append_kb(str,
				    idt,
				    "Ia32TmeActivateLockRo",
				    self->ia32_tme_activation.fields.lock_ro);
		fu_string_append_kb(str,
				    idt,
				    "Ia32TmeActivateEnable",
				    self->ia32_tme_activation.fields.enable);
		fu_string_append_kb(str,
				    idt,
				    "Ia32TmeActivateBypassEnable",
				    self->ia32_tme_activation.fields.bypass_enable);
	}
	if (self->amd64_syscfg_supported) {
		fu_string_append_kb(str,
				    idt,
				    "Amd64SyscfgSmeIsEnabled",
				    self->amd64_syscfg.fields.sme_is_enabled);
	}
	if (self->amd64_sev_supported) {
		fu_string_append_kb(str,
				    idt,
				    "Amd64SevIsEnabled",
				    self->amd64_sev.fields.sev_is_enabled);
	}
}

static gboolean
fu_msr_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
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
	if (fu_cpu_get_vendor() == FU_CPU_VENDOR_INTEL) {
		if (!fu_cpuid(0x01, NULL, NULL, &ecx, NULL, error))
			return FALSE;
		self->ia32_debug_supported = ((ecx >> 11) & 0x1) > 0;
		if (!fu_cpuid(0x07, NULL, NULL, &ecx, NULL, error))
			return FALSE;
		self->ia32_tme_supported = ((ecx >> 13) & 0x1) > 0;
	}

	/* indicates support for SME and SEV */
	if (fu_cpu_get_vendor() == FU_CPU_VENDOR_AMD) {
		if (!fu_cpuid(0x8000001f, &eax, &ebx, NULL, NULL, error))
			return FALSE;
		g_debug("SME/SEV check MSR: eax 0%x, ebx 0%x", eax, ebx);
		self->amd64_syscfg_supported = ((eax >> 0) & 0x1) > 0;
		self->amd64_sev_supported = ((eax >> 1) & 0x1) > 0;
	}

	return TRUE;
}

static gboolean
fu_msr_plugin_backend_device_added(FuPlugin *plugin,
				   FuDevice *device,
				   FuProgress *progress,
				   GError **error)
{
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
	FuDevice *device_cpu = fu_plugin_cache_lookup(plugin, "cpu");
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
	if (self->ia32_debug_supported) {
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(device),
					  PCI_MSR_IA32_DEBUG_INTERFACE,
					  buf,
					  sizeof(buf),
					  error)) {
			g_prefix_error(error, "could not read IA32_DEBUG_INTERFACE: ");
			return FALSE;
		}
		if (!fu_memread_uint32_safe(buf,
					    sizeof(buf),
					    0x0,
					    &self->ia32_debug.data,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}
	if (self->ia32_tme_supported) {
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(device),
					  PCI_MSR_IA32_TME_ACTIVATION,
					  buf,
					  sizeof(buf),
					  error)) {
			g_prefix_error(error, "could not read IA32_TME_ACTIVATION: ");
			return FALSE;
		}
		if (!fu_memread_uint64_safe(buf,
					    sizeof(buf),
					    0x0,
					    &self->ia32_tme_activation.data,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}

	/* grab AMD MSRs */
	if (self->amd64_syscfg_supported) {
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(device),
					  PCI_MSR_AMD64_SYSCFG,
					  buf,
					  sizeof(buf),
					  error)) {
			g_prefix_error(error, "could not read PCI_MSR_AMD64_SYSCFG: ");
			return FALSE;
		}
		if (!fu_memread_uint32_safe(buf,
					    sizeof(buf),
					    0x0,
					    &self->amd64_syscfg.data,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}
	if (self->amd64_sev_supported) {
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(device),
					  PCI_MSR_AMD64_SEV,
					  buf,
					  sizeof(buf),
					  error)) {
			g_prefix_error(error, "could not read PCI_MSR_AMD64_SEV: ");
			return FALSE;
		}
		if (!fu_memread_uint32_safe(buf,
					    sizeof(buf),
					    0x0,
					    &self->amd64_sev.data,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}

	/* get microcode version */
	if (device_cpu != NULL) {
		guint32 ver_raw;
		guint8 offset;

		if (!fu_cpuid(0x1, NULL, NULL, NULL, NULL, error))
			return FALSE;
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(device),
					  PCI_MSR_IA32_BIOS_SIGN_ID,
					  buf,
					  sizeof(buf),
					  error)) {
			g_prefix_error(error, "could not read IA32_BIOS_SIGN_ID: ");
			return FALSE;
		}
		fu_dump_raw(G_LOG_DOMAIN, "IA32_BIOS_SIGN_ID", buf, sizeof(buf));
		offset = fu_cpu_get_vendor() == FU_CPU_VENDOR_AMD ? 0x0 : 0x4;
		if (!fu_memread_uint32_safe(buf,
					    sizeof(buf),
					    offset,
					    &ver_raw,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (ver_raw != 0 && ver_raw != G_MAXUINT32)
			fu_device_set_version_from_uint32(device_cpu, ver_raw);
	}

	/* success */
	return TRUE;
}

static void
fu_msr_plugin_device_registered(FuPlugin *plugin, FuDevice *dev)
{
	if (g_strcmp0(fu_device_get_plugin(dev), "cpu") == 0) {
		fu_plugin_cache_add(plugin, "cpu", dev);
		return;
	}
}

static void
fu_plugin_add_security_attr_dci_enabled(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
	FuDevice *device = fu_plugin_cache_lookup(plugin, "cpu");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED);
	if (device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(device));
	fu_security_attrs_append(attrs, attr);

	/* check fields */
	if (!self->ia32_debug_supported) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		return;
	}
	if (self->ia32_debug.fields.enabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_plugin_add_security_attr_intel_tme_enabled(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* create attr (which should already have been created in the cpu plugin) */
	attr = fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	if (attr == NULL) {
		attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
		fu_security_attrs_append(attrs, attr);
	}

	/* check fields */
	if (!self->ia32_tme_supported) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}
	if (!self->ia32_tme_activation.fields.enable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_remove_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}
	if (self->ia32_tme_activation.fields.bypass_enable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED);
		fwupd_security_attr_remove_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}
	if (!self->ia32_tme_activation.fields.lock_ro) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_remove_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}
}

static void
fu_plugin_add_security_attr_dci_locked(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
	FuDevice *device = fu_plugin_cache_lookup(plugin, "cpu");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED);
	if (device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(device));
	fu_security_attrs_append(attrs, attr);

	/* check fields */
	if (!self->ia32_debug_supported) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		return;
	}
	if (!self->ia32_debug.fields.locked) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static gboolean
fu_msr_plugin_safe_kernel_for_sme(FuPlugin *plugin, GError **error)
{
	g_autofree gchar *min =
	    fu_plugin_get_config_value(plugin,
				       "MinimumSmeKernelVersion",
				       FU_MSR_CONFIG_DEFAULT_MINIMUM_SME_KERNEL_VERSION);
	return fu_kernel_check_version(min, error);
}

static gboolean
fu_msr_plugin_kernel_enabled_sme(GError **error)
{
	g_autofree gchar *buf = NULL;
	gsize bufsz = 0;
	if (!g_file_get_contents("/proc/cpuinfo", &buf, &bufsz, error))
		return FALSE;
	if (bufsz > 0) {
		g_auto(GStrv) tokens = fu_strsplit(buf, bufsz, " ", -1);
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
	FuMsrPlugin *self = FU_MSR_PLUGIN(plugin);
	FuDevice *device = fu_plugin_cache_lookup(plugin, "cpu");
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* this MSR is only valid for a subset of AMD CPUs */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_AMD)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	if (device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(device));
	fu_security_attrs_append(attrs, attr);

	/* check fields */
	if (!self->amd64_syscfg_supported) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	if (!self->amd64_syscfg.fields.sme_is_enabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	if (!fu_msr_plugin_safe_kernel_for_sme(plugin, &error_local)) {
		g_debug("unable to properly detect SME: %s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
		return;
	}

	if (!(fu_msr_plugin_kernel_enabled_sme(&error_local))) {
		g_debug("%s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);
	fwupd_security_attr_add_obsolete(attr, "pci_psp");
}

static void
fu_msr_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	fu_plugin_add_security_attr_dci_enabled(plugin, attrs);
	fu_plugin_add_security_attr_dci_locked(plugin, attrs);
	fu_plugin_add_security_attr_amd_sme_enabled(plugin, attrs);
	fu_plugin_add_security_attr_intel_tme_enabled(plugin, attrs);
}

static void
fu_msr_plugin_init(FuMsrPlugin *self)
{
}

static void
fu_msr_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "msr");
}

static void
fu_msr_plugin_class_init(FuMsrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_msr_plugin_constructed;
	plugin_class->to_string = fu_msr_plugin_to_string;
	plugin_class->startup = fu_msr_plugin_startup;
	plugin_class->backend_device_added = fu_msr_plugin_backend_device_added;
	plugin_class->add_security_attrs = fu_msr_plugin_add_security_attrs;
	plugin_class->device_registered = fu_msr_plugin_device_registered;
}
