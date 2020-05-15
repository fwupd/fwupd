/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

struct FuPluginData {
	gboolean		 has_device;
	guint8			 mei_cfg;
};

#define PCI_CFG_HFS_1		0x40

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "pci");
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "pci") != 0)
		return TRUE;

	/* open the config */
	fu_udev_device_set_flags (device, FU_UDEV_DEVICE_FLAG_USE_CONFIG);
	if (!fu_udev_device_set_physical_id (device, "pci", error))
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* grab MEI config Register */
	if (!fu_udev_device_pread (device, PCI_CFG_HFS_1, &priv->mei_cfg, error)) {
		g_prefix_error (error, "could not read MEI");
		return FALSE;
	}
	priv->has_device = TRUE;
	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;
	if (!priv->has_device)
		return;

	/* create attr */
	attr = fwupd_security_attr_new ("com.intel.MEI");
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "MEI");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if ((priv->mei_cfg & (1 << 4)) != 0) {
		fwupd_security_attr_set_result (attr, "Manufacturing Mode");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}
