/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-gpio-plugin.h"
#include "fu-plugin-private.h"

static gboolean
fu_gpio_test_load_quirks(FuContext *ctx, GError **error)
{
	g_autofree gchar *testdatadir = NULL;
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);
	return fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, error);
}

static void
fu_gpio_plugin_prepare_no_quirk_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_gpio_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_gpio_test_load_quirks(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(device, "gpio");
	fwupd_device_add_guid(FWUPD_DEVICE(device), "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");

	ret = fu_plugin_runner_prepare(plugin, device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_gpio_plugin_prepare_invalid_level_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_gpio_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_gpio_test_load_quirks(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(device, "gpio");
	fwupd_device_add_guid(FWUPD_DEVICE(device), "d4735e3a-265e-516e-be89-2d5d128f1c77");

	ret = fu_plugin_runner_prepare(plugin, device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_gpio_plugin_prepare_invalid_format_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_gpio_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_gpio_test_load_quirks(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(device, "gpio");
	fwupd_device_add_guid(FWUPD_DEVICE(device), "4e074085-27f6-5946-b8a5-3b7e4c26e40f");

	ret = fu_plugin_runner_prepare(plugin, device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_gpio_plugin_prepare_device_not_found_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_gpio_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_gpio_test_load_quirks(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(device, "gpio");
	fwupd_device_add_guid(FWUPD_DEVICE(device), "2ce109e9-5a49-5f0d-9e96-7c157d46b2fb");

	ret = fu_plugin_runner_prepare(plugin, device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
}

static void
fu_gpio_plugin_prepare_multiple_guids_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_gpio_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_gpio_test_load_quirks(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(device, "gpio");
	/* first GUID has no matching quirk, second has an invalid level */
	fwupd_device_add_guid(FWUPD_DEVICE(device), "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fwupd_device_add_guid(FWUPD_DEVICE(device), "d4735e3a-265e-516e-be89-2d5d128f1c77");

	ret = fu_plugin_runner_prepare(plugin, device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_gpio_plugin_cleanup_no_assignments_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_gpio_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_gpio_test_load_quirks(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(device, "gpio");

	ret = fu_plugin_runner_cleanup(plugin, device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/gpio/prepare/no-quirk", fu_gpio_plugin_prepare_no_quirk_func);
	g_test_add_func("/gpio/prepare/invalid-level", fu_gpio_plugin_prepare_invalid_level_func);
	g_test_add_func("/gpio/prepare/invalid-format", fu_gpio_plugin_prepare_invalid_format_func);
	g_test_add_func("/gpio/prepare/device-not-found",
			fu_gpio_plugin_prepare_device_not_found_func);
	g_test_add_func("/gpio/prepare/multiple-guids", fu_gpio_plugin_prepare_multiple_guids_func);
	g_test_add_func("/gpio/cleanup/no-assignments", fu_gpio_plugin_cleanup_no_assignments_func);
	return g_test_run();
}
