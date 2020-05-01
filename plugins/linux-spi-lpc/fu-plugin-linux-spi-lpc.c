/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"


#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#define FU_PLUGIN_LINUX_SPI_LPC_SYSFS_DIR	"/sys/kernel/security/spi"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

static void
fu_plugin_add_security_attr_bioswe (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("org.kernel.BIOSWE");
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "SPI");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	fn = g_build_filename (FU_PLUGIN_LINUX_SPI_LPC_SYSFS_DIR, "bioswe", NULL);
	if (!g_file_get_contents (fn, &buf, &bufsz, &error_local)) {
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, "Could not open file");
		return;
	}
	if (g_strcmp0 (buf, "0\n") != 0) {
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
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("org.kernel.BLE");
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "SPI");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	fn = g_build_filename (FU_PLUGIN_LINUX_SPI_LPC_SYSFS_DIR, "ble", NULL);
	if (!g_file_get_contents (fn, &buf, &bufsz, &error_local)) {
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, "Could not open file");
		return;
	}
	if (g_strcmp0 (buf, "1\n") != 0) {
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
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("org.kernel.SMM_BWP");
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_name (attr, "BIOS region of SPI");
	fu_security_attrs_append (attrs, attr);

	/* load file */
	fn = g_build_filename (FU_PLUGIN_LINUX_SPI_LPC_SYSFS_DIR, "smm_bwp", NULL);
	if (!g_file_get_contents (fn, &buf, &bufsz, &error_local)) {
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, "Could not open file");
		return;
	}
	if (g_strcmp0 (buf, "1\n") != 0) {
		fwupd_security_attr_set_result (attr, "Writable by OS");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, "Writable only through BIOS");
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;

	/* maybe the kernel module does not exist */
	if (!g_file_test (FU_PLUGIN_LINUX_SPI_LPC_SYSFS_DIR, G_FILE_TEST_IS_DIR)) {
		g_autoptr(FwupdSecurityAttr) attr = NULL;
		attr = fwupd_security_attr_new ("org.kernel.BIOSWE");
		fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
		fwupd_security_attr_set_name (attr, "SPI");
		fwupd_security_attr_set_result (attr, "Kernel support not present");
		fu_security_attrs_append (attrs, attr);
		return;
	}

	/* look for the three files in sysfs */
	fu_plugin_add_security_attr_bioswe (plugin, attrs);
	fu_plugin_add_security_attr_ble (plugin, attrs);
	fu_plugin_add_security_attr_smm_bwp (plugin, attrs);
}
