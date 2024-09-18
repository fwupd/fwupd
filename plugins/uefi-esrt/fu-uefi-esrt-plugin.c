/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2017 Dell, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-esrt-plugin.h"

struct _FuUefiEsrtPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiEsrtPlugin, fu_uefi_esrt_plugin, FU_TYPE_PLUGIN)

#define LENOVO_CAPSULE_SETTING "com.thinklmi.WindowsUEFIFirmwareUpdate"
#define DELL_CAPSULE_SETTING   "com.dell.CapsuleFirmwareUpdate"

static gboolean
fu_uefi_esrt_plugin_check_esrt(void)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrtdir = NULL;

	/* already exists */
	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	esrtdir = g_build_filename(sysfsfwdir, "efi", "esrt", NULL);

	return g_file_test(esrtdir, G_FILE_TEST_EXISTS);
}

static void
fu_uefi_esrt_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	if (!fu_efivars_supported(efivars, NULL))
		return;

	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_BIOS_CAPSULE_UPDATES);
	fu_security_attr_add_bios_target_value(attr, LENOVO_CAPSULE_SETTING, "enable");
	fu_security_attr_add_bios_target_value(attr, DELL_CAPSULE_SETTING, "enabled");
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);

	if (fu_uefi_esrt_plugin_check_esrt())
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	else
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);

	fu_security_attrs_append(attrs, attr);
}

static void
fu_uefi_esrt_plugin_init(FuUefiEsrtPlugin *self)
{
	fu_plugin_add_rule(FU_PLUGIN(self), FU_PLUGIN_RULE_BETTER_THAN, "bios");
}

static void
fu_uefi_esrt_plugin_class_init(FuUefiEsrtPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->add_security_attrs = fu_uefi_esrt_plugin_add_security_attrs;
}
