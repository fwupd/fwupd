/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <cpuid.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

typedef union {
	guint32 data;
	struct {
		guint32 enabled			: 1;
		guint32 rsrvd			: 29;
		guint32 locked			: 1;
		guint32 debug_occurred		: 1;
	} __attribute__((packed)) fields;
} FuMsrIa32Debug;

struct FuPluginData {
	gboolean		 ia32_debug_supported;
	FuMsrIa32Debug		 ia32_debug;
};

#define PCI_MSR_IA32_DEBUG_INTERFACE		0xc80

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "msr");
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	guint ecx = 0;

	/* sdbg is supported: https://en.wikipedia.org/wiki/CPUID */
	if (!fu_common_cpuid (0x01, NULL, NULL, &ecx, NULL, error))
		return FALSE;
	priv->ia32_debug_supported = ((ecx >> 11) & 0x1) > 0;
	return TRUE;
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	guint8 buf[8] = { 0x0 };
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree gchar *basename = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "msr") != 0)
		return TRUE;

	/* we only care about the first processor */
	basename = g_path_get_basename (fu_udev_device_get_sysfs_path (device));
	if (g_strcmp0 (basename, "msr0") != 0)
		return TRUE;

	/* open the config */
	fu_device_set_physical_id (FU_DEVICE (device), "msr");
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* grab MSR */
	if (priv->ia32_debug_supported) {
		if (!fu_udev_device_pread_full (device, PCI_MSR_IA32_DEBUG_INTERFACE,
						buf, sizeof(buf), error)) {
			g_prefix_error (error, "could not read IA32_DEBUG_INTERFACE: ");
			return FALSE;
		}
		if (!fu_common_read_uint32_safe (buf, sizeof(buf), 0x0,
						 &priv->ia32_debug.data, G_LITTLE_ENDIAN,
						 error))
			return FALSE;
		g_debug ("IA32_DEBUG_INTERFACE: enabled=%i, locked=%i, debug_occurred=%i",
			 priv->ia32_debug.fields.enabled,
			 priv->ia32_debug.fields.locked,
			 priv->ia32_debug.fields.debug_occurred);
	}
	return TRUE;
}

static void
fu_plugin_add_security_attr_dci_enabled (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (!fu_common_is_cpu_intel ())
		return;
	if (!priv->ia32_debug_supported)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_DCI_ENABLED);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* check fields */
	if (priv->ia32_debug.fields.enabled) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_plugin_add_security_attr_dci_locked (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* this MSR is only valid for a subset of Intel CPUs */
	if (!fu_common_is_cpu_intel ())
		return;
	if (!priv->ia32_debug_supported)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_DCI_LOCKED);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* check fields */
	if (!priv->ia32_debug.fields.locked) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	fu_plugin_add_security_attr_dci_enabled (plugin, attrs);
	fu_plugin_add_security_attr_dci_locked (plugin, attrs);
}
