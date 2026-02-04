/*
 * Copyright 2026 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "fu-context-private.h"
#include "fu-hp-bioscfg-plugin.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"

typedef struct {
	FuContext *ctx;
	FuPlugin *plugin_hp_bioscfg;
} FuTest;

static void
fu_test_self_init(FuTest *self)
{
	gboolean ret;
	g_autofree gchar *confdir = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	confdir = g_test_build_filename(G_TEST_DIST, "tests", "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, confdir);

	ret = fu_context_load_quirks(ctx,
				     FU_QUIRKS_LOAD_FLAG_NO_CACHE | FU_QUIRKS_LOAD_FLAG_NO_VERIFY,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* starting bioscfg dir to make startup pass */
	testdatadir = g_test_build_filename(G_TEST_DIST,
					    "tests",
					    "firmware-attributes",
					    "surestart-not-available",
					    NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);

	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	self->plugin_hp_bioscfg = fu_plugin_new_from_gtype(fu_hp_bioscfg_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(self->plugin_hp_bioscfg, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	self->ctx = fu_plugin_get_context(self->plugin_hp_bioscfg);
}

static void
fu_plugin_hp_bioscfg_surestart_enabled(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autofree gchar *testdatadir = g_test_build_filename(G_TEST_DIST,
							      "tests",
							      "firmware-attributes",
							      "surestart-enabled",
							      NULL);

	fu_context_set_path(self->ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);
	ret = fu_context_reload_bios_settings(self->ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(self->plugin_hp_bioscfg, attrs);

	/* check that SureStart attribute is present and has success status */
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_plugin_hp_bioscfg_surestart_disabled(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autofree gchar *testdatadir = g_test_build_filename(G_TEST_DIST,
							      "tests",
							      "firmware-attributes",
							      "surestart-disabled",
							      NULL);

	fu_context_set_path(self->ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);

	ret = fu_context_reload_bios_settings(self->ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(self->plugin_hp_bioscfg, attrs);

	/* check that SureStart attribute is present and has failure status */
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_true(
	    fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_plugin_hp_bioscfg_surestart_not_available(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autofree gchar *testdatadir = g_test_build_filename(G_TEST_DIST,
							      "tests",
							      "firmware-attributes",
							      "surestart-not-available",
							      NULL);

	fu_context_set_path(self->ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);

	ret = fu_context_reload_bios_settings(self->ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(self->plugin_hp_bioscfg, attrs);

	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_HP_SURESTART, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_test_self_free(FuTest *self)
{
	if (self->plugin_hp_bioscfg != NULL)
		g_object_unref(self->plugin_hp_bioscfg);
	g_free(self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_self_free)
#pragma clang diagnostic pop

int
main(int argc, char **argv)
{
	g_autoptr(FuTest) self = g_new0(FuTest, 1);

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	fu_test_self_init(self);
	g_test_add_data_func("/fwupd/plugin/hp-bioscfg/surestart-enabled",
			     self,
			     fu_plugin_hp_bioscfg_surestart_enabled);
	g_test_add_data_func("/fwupd/plugin/hp-bioscfg/surestart-disabled",
			     self,
			     fu_plugin_hp_bioscfg_surestart_disabled);
	g_test_add_data_func("/fwupd/plugin/hp-bioscfg/surestart-not-available",
			     self,
			     fu_plugin_hp_bioscfg_surestart_not_available);
	return g_test_run();
}
