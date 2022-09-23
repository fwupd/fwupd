/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-bios-plugin.h"

struct _FuBiosPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBiosPlugin, fu_bios_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_bios_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *vendor;

	vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR);
	if (g_strcmp0(vendor, "coreboot") == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "system uses coreboot");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_bios_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrt_path = NULL;

	/* are the EFI dirs set up so we can update each device */
#if defined(__x86_64__) || defined(__i386__)
	g_autoptr(GError) error_local = NULL;
	if (!fu_efivar_supported(&error_local)) {
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_LEGACY_BIOS);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
		return TRUE;
	}
#endif

	/* get the directory of ESRT entries */
	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	esrt_path = g_build_filename(sysfsfwdir, "efi", "esrt", NULL);
	if (!g_file_test(esrt_path, G_FILE_TEST_IS_DIR)) {
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
		return TRUE;
	}

	/* we appear to have UEFI capsule updates */
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED);
	return TRUE;
}

static void
fu_bios_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	if (!fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_LEGACY_BIOS))
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT);
	fu_security_attrs_append(attrs, attr);

	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_bios_plugin_init(FuBiosPlugin *self)
{
}

static void
fu_bios_plugin_class_init(FuBiosPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_bios_plugin_startup;
	plugin_class->coldplug = fu_bios_plugin_coldplug;
	plugin_class->add_security_attrs = fu_bios_plugin_add_security_attrs;
}
