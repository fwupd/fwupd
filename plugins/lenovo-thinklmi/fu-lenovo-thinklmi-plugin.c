/*
 * Copyright (C) 2021 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-lenovo-thinklmi-plugin.h"

struct _FuLenovoThinklmiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLenovoThinklmiPlugin, fu_lenovo_thinklmi_plugin, FU_TYPE_PLUGIN)

#define BIOS_SETTING_SLEEP_MODE	     "com.thinklmi.SleepState"
#define BIOS_SETTING_BOOT_ORDER_LOCK "com.thinklmi.BootOrderLock"
#define BIOS_SETTING_SECURE_ROLLBACK "com.thinklmi.SecureRollBackPrevention"

static gboolean
fu_lenovo_thinklmi_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	const gchar *hwid =
	    fu_context_get_hwid_value(fu_plugin_get_context(plugin), FU_HWIDS_KEY_MANUFACTURER);
	if (g_strcmp0(hwid, "LENOVO") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported manufacturer");
		return FALSE;
	}

	return TRUE;
}

static void
fu_lenovo_thinklmi_plugin_cpu_registered(FuContext *ctx, FuDevice *device)
{
	/* Ryzen 6000 doesn't support S3 even if the BIOS offers it */
	if (fu_device_has_instance_id(device, "CPUID\\PRO_0&FAM_19&MOD_44")) {
		FwupdBiosSetting *attr = fu_context_get_bios_setting(ctx, BIOS_SETTING_SLEEP_MODE);

		if (attr != NULL) {
			g_debug("Setting %s to read-only", fwupd_bios_setting_get_name(attr));
			fwupd_bios_setting_set_read_only(attr, TRUE);
		}
	}
}

static void
fu_lenovo_thinklmi_plugin_uefi_capsule_registered(FuContext *ctx, FuDevice *device)
{
	FwupdBiosSetting *attr;

	/* check if boot order lock is turned on */
	attr = fu_context_get_bios_setting(ctx, BIOS_SETTING_BOOT_ORDER_LOCK);
	if (attr == NULL) {
		g_debug("failed to find %s in cache", BIOS_SETTING_BOOT_ORDER_LOCK);
		return;
	}
	if (g_strcmp0(fwupd_bios_setting_get_current_value(attr), "Enable") == 0) {
		fu_device_inhibit(device,
				  "uefi-capsule-bootorder",
				  "BootOrder is locked in firmware setup");
	}

	/* check if we're pending for a reboot */
	if (fu_context_get_bios_setting_pending_reboot(ctx)) {
		fu_device_inhibit(device,
				  "uefi-capsule-pending-reboot",
				  "UEFI BIOS settings update pending reboot");
	}
}

static void
fu_lenovo_thinklmi_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	if (g_strcmp0(fu_device_get_plugin(device), "uefi_capsule") == 0) {
		fu_lenovo_thinklmi_plugin_uefi_capsule_registered(fu_plugin_get_context(plugin),
								  device);
	} else if (g_strcmp0(fu_device_get_plugin(device), "cpu") == 0) {
		fu_lenovo_thinklmi_plugin_cpu_registered(fu_plugin_get_context(plugin), device);
	}
}

static void
fu_lenovo_thinklmi_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FwupdBiosSetting *bios_attr;
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	bios_attr = fu_context_get_bios_setting(ctx, BIOS_SETTING_SECURE_ROLLBACK);
	if (bios_attr == NULL) {
		g_debug("failed to find %s in cache", BIOS_SETTING_SECURE_ROLLBACK);
		return;
	}

	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION);
	fu_security_attr_add_bios_target_value(attr, BIOS_SETTING_SECURE_ROLLBACK, "enable");
	fu_security_attrs_append(attrs, attr);

	if (g_strcmp0(fwupd_bios_setting_get_current_value(bios_attr), "Disable") == 0) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_lenovo_thinklmi_plugin_init(FuLenovoThinklmiPlugin *self)
{
}

static void
fu_lenovo_thinklmi_plugin_class_init(FuLenovoThinklmiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_lenovo_thinklmi_plugin_startup;
	plugin_class->device_registered = fu_lenovo_thinklmi_plugin_device_registered;
	plugin_class->add_security_attrs = fu_lenovo_thinklmi_plugin_add_security_attrs;
}
