/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-util-common.h"

static void
fu_util_func(void)
{
	const gchar *tmp;
	g_autoptr(FwupdClient) client = fwupd_client_new();
	g_autoptr(FwupdDevice) device = fwupd_device_new();

	for (FwupdDeviceProblem i = 1; i < G_MAXUINT64; i <<= 1) {
		g_autofree gchar *str = NULL;
		tmp = fwupd_device_problem_to_string(i);
		if (tmp == NULL)
			break;
		str = fu_util_device_problem_to_string(client, device, i);
		g_assert_nonnull(str);
	}
	for (FwupdReleaseFlags i = 1; i < G_MAXUINT64; i <<= 1) {
		tmp = fwupd_release_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_nonnull(fu_util_release_flag_to_string(i));
	}
	for (FwupdRequestFlags i = 1; i < G_MAXUINT64; i <<= 1) {
		tmp = fwupd_request_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_nonnull(fu_util_request_flag_to_string(i));
	}
	for (FwupdPluginFlags i = 1; i < G_MAXUINT64; i <<= 1) {
		g_autofree gchar *str = NULL;
		if (i == FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE || i == FWUPD_PLUGIN_FLAG_USER_WARNING)
			continue;
		tmp = fwupd_plugin_flag_to_string(i);
		if (tmp == NULL)
			break;
		str = fu_util_plugin_flag_to_string(i);
		g_assert_nonnull(str);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/util", fu_util_func);
	return g_test_run();
}
