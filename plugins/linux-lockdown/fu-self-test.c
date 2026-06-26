/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-linux-lockdown-plugin.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"

static void
fu_linux_lockdown_none_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    fu_plugin_new_from_gtype(fu_linux_lockdown_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("linux-lockdown-none", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	fn = fu_temporary_directory_build(tmpdir, "lockdown", NULL);
	ret = g_file_set_contents(fn, "[none] integrity confidentiality\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_path_store_set_tmpdir(fu_context_get_path_store(ctx),
				 FU_PATH_KIND_SYSFSDIR_SECURITY,
				 tmpdir);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_linux_lockdown_integrity_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    fu_plugin_new_from_gtype(fu_linux_lockdown_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("linux-lockdown-integrity", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	fn = fu_temporary_directory_build(tmpdir, "lockdown", NULL);
	ret = g_file_set_contents(fn, "none [integrity] confidentiality\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_path_store_set_tmpdir(fu_context_get_path_store(ctx),
				 FU_PATH_KIND_SYSFSDIR_SECURITY,
				 tmpdir);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_linux_lockdown_confidentiality_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    fu_plugin_new_from_gtype(fu_linux_lockdown_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("linux-lockdown-confidentiality", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	fn = fu_temporary_directory_build(tmpdir, "lockdown", NULL);
	ret = g_file_set_contents(fn, "none integrity [confidentiality]\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_path_store_set_tmpdir(fu_context_get_path_store(ctx),
				 FU_PATH_KIND_SYSFSDIR_SECURITY,
				 tmpdir);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_linux_lockdown_unknown_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    fu_plugin_new_from_gtype(fu_linux_lockdown_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("linux-lockdown-unknown", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	fn = fu_temporary_directory_build(tmpdir, "lockdown", NULL);
	ret = g_file_set_contents(fn, "none integrity confidentiality [unknown]\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_path_store_set_tmpdir(fu_context_get_path_store(ctx),
				 FU_PATH_KIND_SYSFSDIR_SECURITY,
				 tmpdir);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_linux_lockdown_missing_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin =
	    fu_plugin_new_from_gtype(fu_linux_lockdown_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_path_store_set_path(fu_context_get_path_store(ctx),
			       FU_PATH_KIND_SYSFSDIR_SECURITY,
			       "/nonexistent");

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/linux-lockdown/none", fu_linux_lockdown_none_func);
	g_test_add_func("/linux-lockdown/integrity", fu_linux_lockdown_integrity_func);
	g_test_add_func("/linux-lockdown/confidentiality", fu_linux_lockdown_confidentiality_func);
	g_test_add_func("/linux-lockdown/unknown", fu_linux_lockdown_unknown_func);
	g_test_add_func("/linux-lockdown/missing", fu_linux_lockdown_missing_func);
	return g_test_run();
}
