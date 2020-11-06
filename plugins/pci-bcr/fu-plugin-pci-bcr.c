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
	guint8			 bcr_addr;
	guint8			 bcr;
};

#define BCR_WPD			(1 << 0)
#define BCR_BLE			(1 << 1)
#define BCR_SMM_BWP		(1 << 5)

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "pci");

	/* this is true except for some Atoms */
	priv->bcr_addr = 0xdc;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *dev)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	if (g_strcmp0 (fu_device_get_plugin (dev), "cpu") == 0) {
		guint tmp = fu_device_get_metadata_integer (dev, "BcrAddr");
		if (tmp != G_MAXUINT && priv->bcr_addr != tmp) {
			g_debug ("overriding BCR addr from 0x%02x to 0x%02x",
				 priv->bcr_addr, tmp);
			priv->bcr_addr = tmp;
		}
	}
}

static void
fu_plugin_add_security_attr_bioswe (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* no device */
	if (!priv->has_device) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* load file */
	if ((priv->bcr & BCR_WPD) == 1) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_plugin_add_security_attr_ble (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no device */
	if (!priv->has_device)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SPI_BLE);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if ((priv->bcr & BCR_BLE) == 0) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_plugin_add_security_attr_smm_bwp (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no device */
	if (!priv->has_device)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if ((priv->bcr & BCR_SMM_BWP) == 0) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* not supported */
	if (priv->bcr_addr == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "BCR not supported on this platform");
		return FALSE;
	}

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

	/* grab BIOS Control Register */
	if (!fu_udev_device_pread (device, priv->bcr_addr, &priv->bcr, error)) {
		g_prefix_error (error, "could not read BCR: ");
		return FALSE;
	}
	priv->has_device = TRUE;
	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;

	/* add attrs */
	fu_plugin_add_security_attr_bioswe (plugin, attrs);
	fu_plugin_add_security_attr_ble (plugin, attrs);
	fu_plugin_add_security_attr_smm_bwp (plugin, attrs);
}
