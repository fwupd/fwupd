/*
 * Copyright 2026 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-amd-gpu-uma.h"

static void
fu_amd_gpu_uma_check_support_no_support_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;

	tmpdir = fu_temporary_directory_new("uma", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	g_assert_false(
	    fu_amd_gpu_uma_check_support(fu_temporary_directory_get_path(tmpdir), &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_amd_gpu_uma_check_support_with_support_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autofree gchar *uma_dir = NULL;
	g_autofree gchar *carveout_file = NULL;
	g_autofree gchar *options_file = NULL;

	tmpdir = fu_temporary_directory_new("uma", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	uma_dir = fu_temporary_directory_build(tmpdir, "uma", NULL);
	g_assert_nonnull(uma_dir);
	ret = fu_path_mkdir(uma_dir, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	carveout_file = fu_temporary_directory_build(tmpdir, "uma", "carveout", NULL);
	g_assert_nonnull(carveout_file);
	ret = g_file_set_contents(carveout_file, "0\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	options_file = fu_temporary_directory_build(tmpdir, "uma", "carveout_options", NULL);
	g_assert_nonnull(options_file);
	ret = g_file_set_contents(options_file, "0: Minimum (512 MB)\n1: (1 GB)\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_true(
	    fu_amd_gpu_uma_check_support(fu_temporary_directory_get_path(tmpdir), &error));
	g_assert_no_error(error);
}

static void
fu_amd_gpu_uma_get_setting_valid_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autofree gchar *uma_dir = NULL;
	g_autofree gchar *carveout_file = NULL;
	g_autofree gchar *options_file = NULL;
	g_autoptr(FwupdBiosSetting) setting = NULL;
	GPtrArray *possible_values = NULL;

	tmpdir = fu_temporary_directory_new("uma", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	uma_dir = fu_temporary_directory_build(tmpdir, "uma", NULL);
	g_assert_nonnull(uma_dir);
	ret = fu_path_mkdir(uma_dir, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	carveout_file = fu_temporary_directory_build(tmpdir, "uma", "carveout", NULL);
	g_assert_nonnull(carveout_file);
	ret = g_file_set_contents(carveout_file, "0\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	options_file = fu_temporary_directory_build(tmpdir, "uma", "carveout_options", NULL);
	g_assert_nonnull(options_file);
	ret = g_file_set_contents(options_file,
				  "0: Minimum (512 MB)\n1: (1 GB)\n2: (2 GB)\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	setting = fu_amd_gpu_uma_get_setting(fu_temporary_directory_get_path(tmpdir), &error);
	g_assert_nonnull(setting);
	g_assert_no_error(error);

	g_assert_cmpstr(fwupd_bios_setting_get_id(setting), ==, "com.amd-gpu.uma_carveout");
	g_assert_cmpstr(fwupd_bios_setting_get_name(setting), ==, "Dedicated Video Memory");
	g_assert_cmpint(fwupd_bios_setting_get_kind(setting),
			==,
			FWUPD_BIOS_SETTING_KIND_ENUMERATION);

	possible_values = fwupd_bios_setting_get_possible_values(setting);
	g_assert_cmpint(possible_values->len, ==, 3);

	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "Minimum (512 MB)");
}

static void
fu_amd_gpu_uma_get_setting_invalid_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdBiosSetting) setting = NULL;
	g_autofree gchar *fn = NULL;

	tmpdir = fu_temporary_directory_new("uma", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	fn = fu_temporary_directory_build(tmpdir, "uma", NULL);
	g_assert_nonnull(fn);

	setting = fu_amd_gpu_uma_get_setting(fn, &error);
	g_assert_null(setting);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_amd_gpu_uma_write_value_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autofree gchar *uma_dir = NULL;
	g_autofree gchar *carveout_file = NULL;
	g_autofree gchar *options_file = NULL;
	g_autofree gchar *carveout_contents = NULL;
	g_autoptr(FwupdBiosSetting) setting = NULL;

	tmpdir = fu_temporary_directory_new("uma", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	uma_dir = fu_temporary_directory_build(tmpdir, "uma", NULL);
	g_assert_nonnull(uma_dir);
	ret = fu_path_mkdir(uma_dir, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	carveout_file = fu_temporary_directory_build(tmpdir, "uma", "carveout", NULL);
	g_assert_nonnull(carveout_file);
	ret = g_file_set_contents(carveout_file, "0\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	options_file = fu_temporary_directory_build(tmpdir, "uma", "carveout_options", NULL);
	g_assert_nonnull(options_file);
	ret = g_file_set_contents(options_file,
				  "0: Minimum (512 MB)\n1: (1 GB)\n2: (2 GB)\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	setting = fu_amd_gpu_uma_get_setting(fu_temporary_directory_get_path(tmpdir), &error);
	g_assert_nonnull(setting);
	g_assert_no_error(error);

	g_assert_true(fwupd_bios_setting_write_value(setting, "(1 GB)", &error));
	g_assert_no_error(error);

	g_assert_true(g_file_get_contents(carveout_file, &carveout_contents, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(g_strstrip(carveout_contents), ==, "1");

	g_assert_cmpstr(fwupd_bios_setting_get_current_value(setting), ==, "(1 GB)");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/amd-gpu-uma/check-support-no-support",
			fu_amd_gpu_uma_check_support_no_support_func);
	g_test_add_func("/amd-gpu-uma/check-support-with-support",
			fu_amd_gpu_uma_check_support_with_support_func);
	g_test_add_func("/amd-gpu-uma/get-setting-valid", fu_amd_gpu_uma_get_setting_valid_func);
	g_test_add_func("/amd-gpu-uma/get-setting-invalid",
			fu_amd_gpu_uma_get_setting_invalid_func);
	g_test_add_func("/amd-gpu-uma/write-value", fu_amd_gpu_uma_write_value_func);

	return g_test_run();
}
