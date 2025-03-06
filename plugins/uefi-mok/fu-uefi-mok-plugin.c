/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-mok-common.h"
#include "fu-uefi-mok-plugin.h"

struct _FuUefiMokPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiMokPlugin, fu_uefi_mok_plugin, FU_TYPE_PLUGIN)

static gchar *
fu_uefi_mok_plugin_get_filename(void)
{
	g_autofree gchar *sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	return g_build_filename(sysfsdir, "efi", "mok-variables", "HSIStatus", NULL);
}

static gboolean
fu_uefi_mok_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autofree gchar *fn = fu_uefi_mok_plugin_get_filename();

	/* sanity check */
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "%s does not exist", fn);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_mok_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *fn = fu_uefi_mok_plugin_get_filename();

	attr = fu_uefi_mok_attr_new(plugin, fn, &error_local);
	if (attr == NULL) {
		g_warning("failed to load %s: %s", fn, error_local->message);
		return;
	}
	fu_security_attrs_append(attrs, attr);
}

static void
fu_uefi_mok_plugin_init(FuUefiMokPlugin *self)
{
}

static void
fu_uefi_mok_plugin_class_init(FuUefiMokPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_uefi_mok_plugin_startup;
	plugin_class->add_security_attrs = fu_uefi_mok_plugin_add_security_attrs;
}
