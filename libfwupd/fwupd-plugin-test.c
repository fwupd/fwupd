/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-plugin.h"
#include "fwupd-test.h"

static void
fwupd_plugin_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdPlugin) plugin1 = fwupd_plugin_new();
	g_autoptr(FwupdPlugin) plugin2 = fwupd_plugin_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	fwupd_plugin_set_name(plugin1, "foo");
	fwupd_plugin_set_flags(plugin1, FWUPD_PLUGIN_FLAG_USER_WARNING);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	fwupd_plugin_remove_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	fwupd_plugin_remove_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	data = fwupd_codec_to_variant(FWUPD_CODEC(plugin1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(plugin2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_plugin_get_name(plugin2), ==, "foo");
	g_assert_cmpint(fwupd_plugin_get_flags(plugin2),
			==,
			FWUPD_PLUGIN_FLAG_USER_WARNING | FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	g_assert_true(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_USER_WARNING));
	g_assert_true(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE));
	g_assert_false(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_NO_HARDWARE));

	str = fwupd_codec_to_string(FWUPD_CODEC(plugin2));
	ret = fu_test_compare_lines(str,
				    "FwupdPlugin:\n"
				    "  Name:                 foo\n"
				    "  Flags:                user-warning|clear-updatable\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/plugin", fwupd_plugin_func);
	return g_test_run();
}
