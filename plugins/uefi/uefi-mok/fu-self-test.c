/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-uefi-mok-common.h"

static void
fu_uefi_mok_nx_disabled_func(void)
{
	g_autofree gchar *str = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "HSIStatus-nx-disabled", NULL);
	attr = fu_uefi_mok_attr_new(plugin, fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);

	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));

	fwupd_security_attr_set_created(attr, 0);
	str = fwupd_codec_to_string(FWUPD_CODEC(attr));
	g_assert_cmpstr(str,
			==,
			"FuSecurityAttr:\n"
			"  AppstreamId:          org.fwupd.hsi.Uefi.MemoryProtection\n"
			"  HsiResult:            not-enabled\n"
			"  HsiResultSuccess:     locked\n"
			"  Flags:                action-config-os\n"
			"  Plugin:               uefi_mok\n"
			"  has-dxe-services-table: 0\n"
			"  has-get-memory-space-descriptor: 0\n"
			"  has-memory-attribute-protocol: 0\n"
			"  has-set-memory-space-attributes: 0\n"
			"  heap-is-executable:   0\n"
			"  ro-sections-are-writable: 0\n"
			"  shim-has-nx-compat-set: 0\n"
			"  stack-is-executable:  0\n");
}

static void
fu_uefi_mok_nx_invalid_func(void)
{
	g_autofree gchar *str = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "HSIStatus-nx-invalid", NULL);
	attr = fu_uefi_mok_attr_new(plugin, fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);

	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));

	fwupd_security_attr_set_created(attr, 0);
	str = fwupd_codec_to_string(FWUPD_CODEC(attr));
	g_assert_cmpstr(str,
			==,
			"FuSecurityAttr:\n"
			"  AppstreamId:          org.fwupd.hsi.Uefi.MemoryProtection\n"
			"  HsiResult:            not-locked\n"
			"  HsiResultSuccess:     locked\n"
			"  Flags:                action-contact-oem\n"
			"  Plugin:               uefi_mok\n"
			"  has-dxe-services-table: 1\n"
			"  has-get-memory-space-descriptor: 0\n"
			"  has-memory-attribute-protocol: 0\n"
			"  has-set-memory-space-attributes: 0\n"
			"  heap-is-executable:   1\n"
			"  ro-sections-are-writable: 1\n"
			"  shim-has-nx-compat-set: 1\n"
			"  stack-is-executable:  1\n"
			"  this-property-does-not-exist: 1\n");
}

static void
fu_uefi_mok_nx_valid_func(void)
{
	g_autofree gchar *str = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "HSIStatus-nx-valid", NULL);
	attr = fu_uefi_mok_attr_new(plugin, fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);

	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));

	fwupd_security_attr_set_created(attr, 0);
	str = fwupd_codec_to_string(FWUPD_CODEC(attr));
	g_assert_cmpstr(str,
			==,
			"FuSecurityAttr:\n"
			"  AppstreamId:          org.fwupd.hsi.Uefi.MemoryProtection\n"
			"  HsiResult:            locked\n"
			"  HsiResultSuccess:     locked\n"
			"  Flags:                success\n"
			"  Plugin:               uefi_mok\n"
			"  has-dxe-services-table: 1\n"
			"  has-get-memory-space-descriptor: 1\n"
			"  has-memory-attribute-protocol: 1\n"
			"  has-set-memory-space-attributes: 1\n"
			"  heap-is-executable:   0\n"
			"  ro-sections-are-writable: 0\n"
			"  shim-has-nx-compat-set: 1\n"
			"  stack-is-executable:  0\n");
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi/mok{nx-disabled}", fu_uefi_mok_nx_disabled_func);
	g_test_add_func("/uefi/mok{nx-invalid}", fu_uefi_mok_nx_invalid_func);
	g_test_add_func("/uefi/mok{nx-valid}", fu_uefi_mok_nx_valid_func);
	return g_test_run();
}
