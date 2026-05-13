/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-plugin-list.h"
#include "fu-plugin-private.h"

static void
fu_plugin_list_func(void)
{
	GPtrArray *plugins;
	FuPlugin *plugin;
	g_autoptr(FuPluginList) plugin_list = fu_plugin_list_new();
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new(NULL);
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;

	fu_plugin_set_name(plugin1, "plugin1");
	fu_plugin_set_name(plugin2, "plugin2");

	/* get all the plugins */
	fu_plugin_list_add(plugin_list, plugin1);
	fu_plugin_list_add(plugin_list, plugin2);
	plugins = fu_plugin_list_get_all(plugin_list);
	g_assert_cmpint(plugins->len, ==, 2);

	/* get a single plugin */
	plugin = fu_plugin_list_find_by_name(plugin_list, "plugin1", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin);
	g_assert_cmpstr(fu_plugin_get_name(plugin), ==, "plugin1");

	/* does not exist */
	plugin = fu_plugin_list_find_by_name(plugin_list, "nope", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(plugin);
}

static void
fu_plugin_list_depsolve_func(void)
{
	GPtrArray *plugins;
	FuPlugin *plugin;
	gboolean ret;
	g_autoptr(FuPluginList) plugin_list = fu_plugin_list_new();
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new(NULL);
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;

	fu_plugin_set_name(plugin1, "plugin1");
	fu_plugin_set_name(plugin2, "plugin2");

	/* add rule then depsolve */
	fu_plugin_list_add(plugin_list, plugin1);
	fu_plugin_list_add(plugin_list, plugin2);
	fu_plugin_add_rule(plugin1, FU_PLUGIN_RULE_RUN_AFTER, "plugin2");
	ret = fu_plugin_list_depsolve(plugin_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	plugins = fu_plugin_list_get_all(plugin_list);
	g_assert_cmpint(plugins->len, ==, 2);
	plugin = g_ptr_array_index(plugins, 0);
	g_assert_cmpstr(fu_plugin_get_name(plugin), ==, "plugin2");
	g_assert_cmpint(fu_plugin_get_order(plugin), ==, 0);
	g_assert_false(fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED));

	/* add another rule, then re-depsolve */
	fu_plugin_add_rule(plugin1, FU_PLUGIN_RULE_CONFLICTS, "plugin2");
	ret = fu_plugin_list_depsolve(plugin_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	plugin = fu_plugin_list_find_by_name(plugin_list, "plugin1", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin);
	g_assert_false(fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED));
	plugin = fu_plugin_list_find_by_name(plugin_list, "plugin2", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin);
	g_assert_true(fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/plugin-list", fu_plugin_list_func);
	g_test_add_func("/fwupd/plugin-list/depsolve", fu_plugin_list_depsolve_func);
	return g_test_run();
}
