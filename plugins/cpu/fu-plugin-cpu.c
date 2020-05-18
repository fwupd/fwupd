/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-cpu-device.h"

struct FuPluginData {
	gboolean		 has_cet;
	gboolean		 has_tme;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize length;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;

	if (!g_file_get_contents ("/proc/cpuinfo", &buf, &length, error))
		return FALSE;

	lines = g_strsplit (buf, "\n\n", 0);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_autoptr(FuCpuDevice) dev = NULL;
		if (strlen (lines[i]) == 0)
			continue;
		dev = fu_cpu_device_new (lines[i]);
		if (!fu_device_setup (FU_DEVICE (dev), error))
			return FALSE;
		if (fu_cpu_device_has_flag (dev, FU_CPU_DEVICE_FLAG_SHSTK) &&
		    fu_cpu_device_has_flag (dev, FU_CPU_DEVICE_FLAG_IBT))
			data->has_cet = TRUE;
		if (fu_cpu_device_has_flag (dev, FU_CPU_DEVICE_FLAG_TME))
			data->has_tme = TRUE;
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	return TRUE;
}

static void
fu_plugin_add_security_attrs_intel_cet (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_CET);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);

	/* check for CET */
	if (!data->has_cet) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_plugin_add_security_attrs_intel_tme (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION);
	fu_security_attrs_append (attrs, attr);

	/* check for TME */
	if (!data->has_tme) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;

	fu_plugin_add_security_attrs_intel_cet (plugin, attrs);
	fu_plugin_add_security_attrs_intel_tme (plugin, attrs);
}
