/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-tpm-device.h"

struct FuPluginData {
	gboolean		 has_tpm;
	gboolean		 has_tpm_v20;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_udev_subsystem (ctx, "tpm");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_TPM_DEVICE);
}

void
fu_plugin_device_added (FuPlugin *plugin, FuDevice *dev)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *family = fu_tpm_device_get_family (FU_TPM_DEVICE (dev));

	data->has_tpm = TRUE;
	if (g_strcmp0 (family, "2.0") == 0)
		data->has_tpm_v20 = TRUE;
	fu_plugin_add_report_metadata (plugin, "TpmFamily", family);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* check exists, and in v2.0 mode */
	if (!data->has_tpm) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}
	if (!data->has_tpm_v20) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
}
