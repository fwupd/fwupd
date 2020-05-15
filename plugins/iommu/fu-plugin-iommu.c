/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hash.h"
#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	gboolean		 has_iommu;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "iommu");
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "iommu") != 0)
		return TRUE;
	priv->has_iommu = TRUE;

	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("org.kernel.IOMMU");
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_name (attr, "IOMMU");
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fu_security_attrs_append (attrs, attr);

	if (!data->has_iommu) {
		fwupd_security_attr_set_result (attr, "Not found");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}
