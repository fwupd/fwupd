/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-security-attr-private.h"

#include "fu-context-private.h"
#include "fu-efivars-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-uefi-ssp-policy-plugin.h"
#include "fu-volume-private.h"

static void
fu_uefi_ssp_policy_plugin_func(void)
{
	gboolean ret;
	const gchar *tmpdir = g_getenv("FWUPD_LOCALSTATEDIR");
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs1 = fu_security_attrs_new();
	g_autoptr(FuSecurityAttrs) attrs2 = fu_security_attrs_new();
	g_autoptr(FuVolume) volume = fu_volume_new_from_mount_path(tmpdir);
	g_autoptr(FwupdSecurityAttr) attr1 = NULL;
	g_autoptr(FwupdSecurityAttr) attr2 = NULL;
	g_autoptr(GBytes) skusi_blob = g_bytes_new_static("hello", 5);
	g_autoptr(GError) error = NULL;
	FuEfivars *efivars = fu_context_get_efivars(ctx);

	/* add a plausible ESP */
	fu_volume_set_partition_kind(volume, FU_VOLUME_KIND_ESP);
	fu_volume_set_partition_uuid(volume, "41f5e9b7-eb4f-5c65-b8a6-f94b0ad54815");
	fu_context_add_esp_volume(ctx, volume);

	/* set up system */
	ret = fu_efivars_set_secure_boot(efivars, 0x01, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0001,
						      volume,
						      "Fedora",
						      "grubx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0002,
						      volume,
						      "Windows",
						      "bootmgfw.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_build_boot_order(efivars, &error, 0x0001, 0x0002, G_MAXUINT16);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_boot_current(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin */
	plugin = fu_plugin_new_from_gtype(fu_uefi_ssp_policy_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* initially missing */
	g_assert_false(
	    fu_efivars_exists(efivars, FU_EFI_SIGNATURE_GUID_MICROSOFT, "SkuSiPolicyVersion"));
	g_assert_false(fu_efivars_exists(efivars,
					 FU_EFI_SIGNATURE_GUID_MICROSOFT,
					 "SkuSiPolicyUpdateSigners"));

	/* verify HSI attributes */
	fu_plugin_runner_add_security_attrs(plugin, attrs1);
	attr1 = fu_security_attrs_get_by_appstream_id(attrs1,
						      FWUPD_SECURITY_ATTR_ID_SSP_POLICY_VARS,
						      &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr1);
	g_assert_cmpint(fwupd_security_attr_get_result(attr1),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);

	/* check we detected Windows 10 */
	g_assert_false(fwupd_security_attr_has_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX));
	g_assert_false(fwupd_security_attr_has_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));

	/* fix it anyway */
	ret = fu_plugin_runner_fix_host_security_attr(plugin, attr1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* lets pretend to reboot, and shim created the vars for us */
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFI_SIGNATURE_GUID_MICROSOFT,
					"SkuSiPolicyVersion",
					skusi_blob,
					FU_EFIVARS_ATTR_NON_VOLATILE |
					    FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFI_SIGNATURE_GUID_MICROSOFT,
					"SkuSiPolicyUpdateSigners",
					skusi_blob,
					FU_EFIVARS_ATTR_NON_VOLATILE |
					    FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check all okay */
	fu_plugin_runner_add_security_attrs(plugin, attrs2);
	attr2 = fu_security_attrs_get_by_appstream_id(attrs2,
						      FWUPD_SECURITY_ATTR_ID_SSP_POLICY_VARS,
						      &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr2);
	g_assert_cmpint(fwupd_security_attr_get_result(attr2),
			==,
			FWUPD_SECURITY_ATTR_RESULT_FOUND);
	g_assert_false(fwupd_security_attr_has_flag(attr2, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX));
	g_assert_true(fwupd_security_attr_has_flag(attr2, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("FWUPD_LOCALSTATEDIR", "/tmp", TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);

	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/uefi-ssp-policy/hsi", fu_uefi_ssp_policy_plugin_func);
	return g_test_run();
}
