/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-dmar-plugin.h"
#include "fu-acpi-dmar.h"
#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"

static void
fu_acpi_dmar_opt_in_func(void)
{
	gboolean ret;
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdFileInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "DMAR", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing DMAR");
		return;
	}
	stream = fwupd_file_input_stream_new(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(dmar),
				       FWUPD_INPUT_STREAM(stream),
				       0x0,
				       FU_FIRMWARE_PARSE_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_acpi_dmar_get_opt_in(dmar));
}

static void
fu_acpi_dmar_opt_out_func(void)
{
	gboolean ret;
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdFileInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "DMAR-OPTOUT", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing DMAR-OPTOUT");
		return;
	}
	stream = fwupd_file_input_stream_new(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(dmar),
				       FWUPD_INPUT_STREAM(stream),
				       0x0,
				       FU_FIRMWARE_PARSE_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_acpi_dmar_get_opt_in(dmar));
}

static void
fu_acpi_dmar_plugin_opt_in_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("acpi-dmar", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_ACPI_TABLES, tmpdir);
	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);

	/* write a DMAR table */
	fu_acpi_dmar_set_opt_in(dmar, TRUE);
	blob = fu_firmware_write(FU_FIRMWARE(dmar), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	fn = fu_temporary_directory_build(tmpdir, "DMAR", NULL);
	ret = fu_bytes_set_contents(fn, blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_acpi_dmar_plugin_get_type(), ctx);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_acpi_dmar_plugin_opt_out_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("acpi-dmar", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_ACPI_TABLES, tmpdir);
	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);

	/* write a DMAR table */
	fu_acpi_dmar_set_opt_in(dmar, FALSE);
	blob = fu_firmware_write(FU_FIRMWARE(dmar), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	fn = fu_temporary_directory_build(tmpdir, "DMAR", NULL);
	ret = fu_bytes_set_contents(fn, blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_acpi_dmar_plugin_get_type(), ctx);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM));
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
}

static void
fu_acpi_dmar_plugin_missing_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("acpi-dmar", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_ACPI_TABLES, tmpdir);
	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);

	plugin = fu_plugin_new_from_gtype(fu_acpi_dmar_plugin_get_type(), ctx);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_acpi_dmar_plugin_amd_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("acpi-dmar", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_ACPI_TABLES, tmpdir);
	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);

	/* write a DMAR table */
	fu_acpi_dmar_set_opt_in(dmar, TRUE);
	blob = fu_firmware_write(FU_FIRMWARE(dmar), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	fn = fu_temporary_directory_build(tmpdir, "DMAR", NULL);
	ret = fu_bytes_set_contents(fn, blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_acpi_dmar_plugin_get_type(), ctx);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
						     NULL);
	g_assert_null(attr);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/acpi-dmar/opt-in", fu_acpi_dmar_opt_in_func);
	g_test_add_func("/acpi-dmar/opt-out", fu_acpi_dmar_opt_out_func);
	g_test_add_func("/acpi-dmar/plugin/opt-in", fu_acpi_dmar_plugin_opt_in_func);
	g_test_add_func("/acpi-dmar/plugin/opt-out", fu_acpi_dmar_plugin_opt_out_func);
	g_test_add_func("/acpi-dmar/plugin/missing", fu_acpi_dmar_plugin_missing_func);
	g_test_add_func("/acpi-dmar/plugin/amd", fu_acpi_dmar_plugin_amd_func);
	return g_test_run();
}
