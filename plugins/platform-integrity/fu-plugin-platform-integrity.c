/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

struct FuPluginData {
	gchar			*sysfs_path;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_udev_subsystem (ctx, "platform-integrity");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_free (priv->sysfs_path);
}

gboolean
fu_plugin_backend_device_added (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE (device))
		return TRUE;
	if (g_strcmp0 (fu_udev_device_get_subsystem (FU_UDEV_DEVICE (device)), "platform-integrity") != 0)
		return TRUE;

	/* we only care about the first instance */
	if (priv->sysfs_path != NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "only one platform-integrity device supported; already using %s",
			     priv->sysfs_path);
		return FALSE;
	}

	/* success */
	priv->sysfs_path = g_strdup (fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device)));
	return TRUE;
}

static void
fu_plugin_add_security_attr_bioswe (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_obsolete (attr, "pci_bcr");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	fn = g_build_filename (priv->sysfs_path, "bioswe", NULL);
	if (!g_file_get_contents (fn, &buf, &bufsz, &error_local)) {
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (g_strcmp0 (buf, "0\n") != 0) {
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
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SPI_BLE);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_obsolete (attr, "pci_bcr");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	fn = g_build_filename (priv->sysfs_path, "biosle", NULL);
	if (!g_file_get_contents (fn, &buf, &bufsz, &error_local)) {
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (g_strcmp0 (buf, "1\n") != 0) {
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
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_obsolete (attr, "pci_bcr");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	fn = g_build_filename (priv->sysfs_path, "smm_bioswp", NULL);
	if (!g_file_get_contents (fn, &buf, &bufsz, &error_local)) {
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (g_strcmp0 (buf, "1\n") != 0) {
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
	FuPluginData *priv = fu_plugin_get_data (plugin);

	/* only when the kernel module is available */
	if (priv->sysfs_path == NULL)
		return;

	/* look for the three files in sysfs */
	fu_plugin_add_security_attr_bioswe (plugin, attrs);
	fu_plugin_add_security_attr_ble (plugin, attrs);
	fu_plugin_add_security_attr_smm_bwp (plugin, attrs);
}
