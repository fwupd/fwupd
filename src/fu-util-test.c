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

static void
fu_util_interesting_device_func(void)
{
	struct {
		const gchar *name;
		FwupdDeviceFlags flags;
		const gchar *version;
		const gchar *update_error;
		FwupdDeviceFlags child_flags;
		gboolean expected;
	} map[] = {
	    {
		"updatable",
		FWUPD_DEVICE_FLAG_UPDATABLE,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"update-error",
		FWUPD_DEVICE_FLAG_INTERNAL,
		NULL,
		"failed",
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"version",
		FWUPD_DEVICE_FLAG_INTERNAL,
		"1.2.3",
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"get-details",
		FWUPD_DEVICE_FLAG_NONE,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		TRUE,
	    },
	    {
		"unrelated-flag",
		FWUPD_DEVICE_FLAG_INTERNAL,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_NONE,
		FALSE,
	    },
	    {
		"interesting-child",
		FWUPD_DEVICE_FLAG_INTERNAL,
		NULL,
		NULL,
		FWUPD_DEVICE_FLAG_UPDATABLE,
		TRUE,
	    },
	};

	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		g_autoptr(FwupdDevice) child = NULL;
		g_autoptr(FwupdDevice) device = fwupd_device_new();
		g_autoptr(GPtrArray) devices =
		    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

		g_test_message("checking %s", map[i].name);
		if (map[i].flags != FWUPD_DEVICE_FLAG_NONE)
			fwupd_device_add_flag(device, map[i].flags);
		if (map[i].version != NULL)
			fwupd_device_set_version(device, map[i].version);
		if (map[i].update_error != NULL)
			fwupd_device_set_update_error(device, map[i].update_error);
		g_ptr_array_add(devices, g_object_ref(device));

		if (map[i].child_flags != FWUPD_DEVICE_FLAG_NONE) {
			child = fwupd_device_new();
			fwupd_device_add_flag(child, map[i].child_flags);
			fwupd_device_set_parent(child, device);
			g_ptr_array_add(devices, g_object_ref(child));
		}

		g_assert_cmpint(fu_util_is_interesting_device(devices, device), ==, map[i].expected);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/util", fu_util_func);
	g_test_add_func("/fwupd/util/interesting-device", fu_util_interesting_device_func);
	return g_test_run();
}
