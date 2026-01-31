/*
 * Copyright 2026 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-hp-bioscfg-plugin.h"

struct _FuHpBiosCfgPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHpBiosCfgPlugin, fu_hp_bioscfg_plugin, FU_TYPE_PLUGIN)

#define BIOS_SETTING_SURESTART                                                                     \
	"com.hp-bioscfg.Enhanced_HP_Firmware_Runtime_Intrusion_Prevention_and_Detection"

static gboolean
fu_hp_bioscfg_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	const gchar *hwid =
	    fu_context_get_hwid_value(fu_plugin_get_context(plugin), FU_HWIDS_KEY_MANUFACTURER);
	if (g_strcmp0(hwid, "HP") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported manufacturer, got %s",
			    hwid);
		return FALSE;
	}

	return TRUE;
}

static void
fu_hp_bioscfg_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FwupdBiosSetting *bios_attr;
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuBiosSettings) bios_settings = NULL;

	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
		return;

	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_HP_SURESTART);
	fu_security_attr_add_bios_target_value(attr, BIOS_SETTING_SURESTART, "Enable");
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* no settings supported, hp-bioscfg is missing, we don't know if we have SureStart */
	bios_settings = fu_context_get_bios_settings(ctx);
	if (!fu_bios_settings_is_supported(bios_settings)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
		return;
	}

	/* hp-bioscfg found but didn't find SureStart, should be a failure */
	bios_attr = fu_context_get_bios_setting(ctx, BIOS_SETTING_SURESTART);
	if (bios_attr == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* attribute found; show it's status */
	if (g_strcmp0(fwupd_bios_setting_get_current_value(bios_attr), "Disable") == 0) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_obsolete(attr, FWUPD_SECURITY_ATTR_ID_AMD_PLATFORM_SECURE_BOOT);
}

static void
fu_hp_bioscfg_plugin_init(FuHpBiosCfgPlugin *self)
{
}

static void
fu_hp_bioscfg_plugin_class_init(FuHpBiosCfgPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_hp_bioscfg_plugin_startup;
	plugin_class->add_security_attrs = fu_hp_bioscfg_plugin_add_security_attrs;
}
