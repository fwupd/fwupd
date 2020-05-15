/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <glib/gstdio.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

struct FuPluginData {
	gboolean		 has_device;
	guint8			 bcr;
};

#define BCR			0xdc
#define BCR_WPD			(1 << 0)
#define BCR_BLE			(1 << 1)
#define BCR_SMM_BWP		(1 << 5)

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "pci");
}

static void
fu_plugin_add_security_attr_bioswe (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("com.intel.BIOSWE");
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "SPI");
	fwupd_security_attr_add_obsolete (attr, "org.kernel.BIOSWE");
	fwupd_security_attr_add_obsolete (attr, "org.fwupd.plugin.linux-spi-lpc");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if ((priv->bcr & BCR_WPD) == 1) {
		fwupd_security_attr_set_result (attr, "Write enabled");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, "Write disabled");
}

static void
fu_plugin_add_security_attr_ble (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("com.intel.BLE");
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "SPI");
	fwupd_security_attr_add_obsolete (attr, "org.kernel.BLE");
	fwupd_security_attr_add_obsolete (attr, "org.fwupd.plugin.linux-spi-lpc");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if ((priv->bcr & BCR_BLE) == 0) {
		fwupd_security_attr_set_result (attr, "Lock disabled");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, "Lock enabled");
}

static void
fu_plugin_add_security_attr_smm_bwp (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("com.intel.SMM_BWP");
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "BIOS region of SPI");
	fwupd_security_attr_add_obsolete (attr, "org.kernel.SMM_BWP");
	fwupd_security_attr_add_obsolete (attr, "org.fwupd.plugin.linux-spi-lpc");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if ((priv->bcr & BCR_SMM_BWP) == 0) {
		fwupd_security_attr_set_result (attr, "Writable by OS");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, "Writable only through BIOS");
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
#ifndef _WIN32
	FuPluginData *priv = fu_plugin_get_data (plugin);
	gint fd;
	g_autofree gchar *fn = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "pci") != 0)
		return TRUE;

	/* open config */
	fn = g_build_filename (fu_udev_device_get_sysfs_path (device), "config", NULL);
	fd = g_open (fn, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "could not open %s", fn);
		return FALSE;
	}

	/* grab BIOS Control Register */
	if (pread (fd, &priv->bcr, 0x01, BCR) != 1) {
		g_close (fd, NULL);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "could not read BCR from %s",
			     fn);
		return FALSE;
	}
	priv->has_device = TRUE;
	return g_close (fd, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "pci reads not currently supported on Windows");
	return FALSE;
#endif
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;

	/* only Intel */
	if (!priv->has_device) {
		g_autoptr(FwupdSecurityAttr) attr = NULL;
		attr = fwupd_security_attr_new ("org.fwupd.plugin.pci-bcr");
		fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
		fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
		fwupd_security_attr_set_name (attr, "SPI");
		fwupd_security_attr_set_result (attr, "No PCI devices with BCR");
		fu_security_attrs_append (attrs, attr);
		return;
	}

	/* add attrs */
	fu_plugin_add_security_attr_bioswe (plugin, attrs);
	fu_plugin_add_security_attr_ble (plugin, attrs);
	fu_plugin_add_security_attr_smm_bwp (plugin, attrs);
}
