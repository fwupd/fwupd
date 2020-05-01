/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hash.h"
#include "fu-plugin-vfuncs.h"

#include "fu-tpm-device.h"

struct FuPluginData {
	gboolean		 has_tpm;
	gboolean		 has_tpm_v20;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "tpm");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_TPM_DEVICE);
}

void
fu_plugin_device_added (FuPlugin *plugin, FuDevice *dev)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	data->has_tpm = TRUE;
	if (g_strcmp0 (fu_tpm_device_get_family (FU_TPM_DEVICE (dev)), "2.0") == 0)
		data->has_tpm_v20 = TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("org.trustedcomputinggroup.Tpm");
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "TPM");
	fu_security_attrs_append (attrs, attr);

	/* check exists, and in v2.0 mode */
	if (!data->has_tpm) {
		fwupd_security_attr_set_result (attr, "Not found");
		return;
	}
	if (!data->has_tpm_v20) {
		fwupd_security_attr_set_result (attr, "Not in v2.0 mode");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, "v2.0");
}
