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
fu_uefi_mok_plugin_get_filename(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	return fu_context_build_filename(ctx,
					 error,
					 FU_PATH_KIND_SYSFSDIR_FW,
					 "efi",
					 "mok-variables",
					 "HSIStatus",
					 NULL);
}

static gboolean
fu_uefi_mok_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autofree gchar *fn = NULL;

	/* sanity check */
	fn = fu_uefi_mok_plugin_get_filename(plugin, error);
	if (fn == NULL)
		return FALSE;
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
	g_autoptr(FwupdSecurityAttr) attr_fw = NULL;
	g_autoptr(FwupdSecurityAttr) attr_nx = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *fn = NULL;

	fn = fu_uefi_mok_plugin_get_filename(plugin, NULL);
	if (fn == NULL)
		return;
	blob = fu_bytes_get_contents(fn, &error_local);
	if (blob == NULL) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE))
			g_warning("failed to load %s: %s", fn, error_local->message);
		return;
	}
	attr_fw = fu_uefi_mok_attr_fw_new(plugin, blob);
	fu_security_attrs_append(attrs, attr_fw);
	attr_nx = fu_uefi_mok_attr_nx_new(plugin, blob);
	fu_security_attrs_append(attrs, attr_nx);
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
