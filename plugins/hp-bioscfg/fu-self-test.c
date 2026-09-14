/*
 * Copyright 2026 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "../linux-fwattr/fu-linux-fwattr-plugin.h"
#include "fu-context-private.h"
#include "fu-hp-bioscfg-plugin.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"

static FuContext *
fu_hp_bioscfg_context_new(void)
{
	g_autofree gchar *confdir = NULL;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_NO_CACHE);

	confdir = g_test_build_filename(G_TEST_DIST, "tests", "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, confdir);
	return g_steal_pointer(&ctx);
}

static FuPlugin *
fu_hp_bioscfg_plugin_new(FuContext *ctx)
{
	return fu_plugin_new_from_gtype(fu_hp_bioscfg_plugin_get_type(), ctx);
}

static FuPlugin *
fu_lenovo_thinklmi_linux_fwattr_plugin_new(FuContext *ctx)
{
	return fu_plugin_new_from_gtype(fu_linux_fwattr_plugin_get_type(), ctx);
}

static void
fu_plugin_surestart_enabled(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_hp_bioscfg_context_new();
	g_autoptr(FuPlugin) plugin = fu_hp_bioscfg_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_linux_fwattr = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST,
					    "tests",
					    "firmware-attributes",
					    "surestart-enabled",
					    NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_startup(plugin_linux_fwattr, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	/* check that SureStart attribute is present and has success status */
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_plugin_surestart_enabled_legacy(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_hp_bioscfg_context_new();
	g_autoptr(FuPlugin) plugin = fu_hp_bioscfg_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_linux_fwattr = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST,
					    "tests",
					    "firmware-attributes",
					    "surestart-enabled-legacy",
					    NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_startup(plugin_linux_fwattr, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	/* check that SureStart attribute is present and has success status via legacy attribute */
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_plugin_surestart_disabled(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_hp_bioscfg_context_new();
	g_autoptr(FuPlugin) plugin = fu_hp_bioscfg_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_linux_fwattr = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST,
					    "tests",
					    "firmware-attributes",
					    "surestart-disabled",
					    NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_startup(plugin_linux_fwattr, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	/* check that SureStart attribute is present and has failure status */
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_plugin_surestart_not_available(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_hp_bioscfg_context_new();
	g_autoptr(FuPlugin) plugin = fu_hp_bioscfg_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin_linux_fwattr = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST,
					    "tests",
					    "firmware-attributes",
					    "surestart-not-available",
					    NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_startup(plugin_linux_fwattr, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/plugin/hp-bioscfg/surestart-enabled", fu_plugin_surestart_enabled);
	g_test_add_func("/fwupd/plugin/hp-bioscfg/surestart-enabled-legacy",
			fu_plugin_surestart_enabled_legacy);
	g_test_add_func("/fwupd/plugin/hp-bioscfg/surestart-disabled",
			fu_plugin_surestart_disabled);
	g_test_add_func("/fwupd/plugin/hp-bioscfg/surestart-not-available",
			fu_plugin_surestart_not_available);
	return g_test_run();
}
