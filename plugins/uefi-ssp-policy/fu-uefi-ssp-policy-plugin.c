/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-ssp-policy-plugin.h"

struct _FuUefiSspPolicyPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiSspPolicyPlugin, fu_uefi_ssp_policy_plugin, FU_TYPE_PLUGIN)

typedef enum {
	FU_UEFI_SSP_POLICY_SHIM_LATEST = 1,
	FU_UEFI_SSP_POLICY_SHIM_AUTOMATIC = 2,
	FU_UEFI_SSP_POLICY_SHIM_DELETE = 3,
} FuUefiSspPolicyShim;

static gboolean
fu_uefi_ssp_policy_plugin_bootmgr_found(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GPtrArray) esp_files = NULL;
	g_autoptr(GError) error_local = NULL;

	esp_files = fu_context_get_esp_files(ctx,
					     FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE,
					     &error_local);
	if (esp_files == NULL) {
		g_debug("failed to get files on ESP: %s", error_local->message);
		return FALSE;
	}
	for (guint i = 0; i < esp_files->len; i++) {
		FuFirmware *esp_file = g_ptr_array_index(esp_files, i);
		const gchar *fn = fu_firmware_get_filename(esp_file);
		if (fn != NULL && g_strstr_len(fn, -1, "bootmgfw") != NULL)
			return TRUE;
	}
	return FALSE;
}

static void
fu_uefi_ssp_policy_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(FuSecurityAttr) attr = NULL;

	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SSP_POLICY_VARS);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
	fu_security_attrs_append(attrs, attr);

	/* either key is not found */
	if (!fu_efivars_exists(efivars, FU_EFI_SIGNATURE_GUID_MICROSOFT, "SkuSiPolicyVersion") ||
	    !fu_efivars_exists(efivars,
			       FU_EFI_SIGNATURE_GUID_MICROSOFT,
			       "SkuSiPolicyUpdateSigners")) {
		/* is bootmgr listed in BootOrder? */
		if (fu_uefi_ssp_policy_plugin_bootmgr_found(plugin)) {
			fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
			return;
		}

		/* can we fix this? */
		if (!fu_efivars_exists(efivars, FU_EFIVARS_GUID_SHIM, "SSPPolicy"))
			fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX);
		fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* success */
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static gboolean
fu_uefi_ssp_policy_plugin_fix_host_security_attr(FuPlugin *plugin,
						 FuSecurityAttr *attr,
						 GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	guint8 val = FU_UEFI_SSP_POLICY_SHIM_LATEST;

	/* shim will do the right thing on next boot */
	if (!fu_efivars_set_data(efivars,
				 FU_EFIVARS_GUID_SHIM,
				 "SSPPolicy",
				 &val,
				 sizeof(val),
				 FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				     FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				     FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				 error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_ssp_policy_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuEfiSecureBootState secureboot_state = FU_EFI_SECURE_BOOT_STATE_DISABLED;

	if (!fu_efivars_get_secure_boot(efivars, &secureboot_state, error))
		return FALSE;
	if ((secureboot_state & FU_EFI_SECURE_BOOT_STATE_ENABLED) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "SecureBoot is not enabled");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_ssp_policy_plugin_init(FuUefiSspPolicyPlugin *self)
{
}

static void
fu_uefi_ssp_policy_plugin_class_init(FuUefiSspPolicyPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_uefi_ssp_policy_plugin_startup;
	plugin_class->add_security_attrs = fu_uefi_ssp_policy_plugin_add_security_attrs;
	plugin_class->fix_host_security_attr = fu_uefi_ssp_policy_plugin_fix_host_security_attr;
}
