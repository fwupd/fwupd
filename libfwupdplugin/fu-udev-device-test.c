/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-udev-device-private.h"

static void
fu_udev_device_func(void)
{
	g_autofree gchar *prop = NULL;
	g_autofree gchar *sysfs_path = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuUdevDevice) udev_device = fu_udev_device_new(ctx, sysfs_path);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) attrs = NULL;

	prop = fu_udev_device_read_property(udev_device, "MODALIAS", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(prop, ==, "hdaudio:v10EC0298r00100103a01");

	/* list all the files in the directory */
	attrs = fu_udev_device_list_sysfs(udev_device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attrs);
	g_assert_cmpint(attrs->len, >, 10);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/udev-device", fu_udev_device_func);
	return g_test_run();
}
