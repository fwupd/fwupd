/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-udev-backend.h"
#include "fu-udev-device-private.h"

static void
fu_udev_backend_strsplit_nul_func(void)
{
	const gchar buf[] = "ACTION=add\0DEVPATH=/devices/foo\0SUBSYSTEM=usb\0";
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf) - 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) values = NULL;

	values = fu_udev_backend_strsplit_nul(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(values);
	g_assert_cmpint(values->len, ==, 3);
	g_assert_cmpstr(g_ptr_array_index(values, 0), ==, "ACTION=add");
	g_assert_cmpstr(g_ptr_array_index(values, 1), ==, "DEVPATH=/devices/foo");
	g_assert_cmpstr(g_ptr_array_index(values, 2), ==, "SUBSYSTEM=usb");
}

static void
fu_udev_backend_strsplit_nul_empty_values_func(void)
{
	/* leading, consecutive and trailing NULs do not add empty strings */
	const gchar buf[] = "\0FOO=bar\0\0BAZ=qux\0\0";
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf) - 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) values = NULL;

	values = fu_udev_backend_strsplit_nul(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(values);
	g_assert_cmpint(values->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(values, 0), ==, "FOO=bar");
	g_assert_cmpstr(g_ptr_array_index(values, 1), ==, "BAZ=qux");
}

static void
fu_udev_backend_strsplit_nul_unterminated_func(void)
{
	/* trailing data without a NUL terminator means the uevent was truncated */
	const gchar buf[] = "FOO=bar\0BAZ=qux";
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf) - 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) values = NULL;

	values = fu_udev_backend_strsplit_nul(blob, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(values);
}

static void
fu_udev_backend_strsplit_nul_no_nul_func(void)
{
	const gchar buf[] = "FOO=bar";
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, sizeof(buf) - 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) values = NULL;

	values = fu_udev_backend_strsplit_nul(blob, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(values);
}

static void
fu_udev_backend_strsplit_nul_invalid_func(void)
{
	g_autoptr(GBytes) blob = g_bytes_new_static("", 0);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) values = NULL;

	values = fu_udev_backend_strsplit_nul(blob, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(values);
}

static void
fu_udev_backend_devname_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuUdevDevice) device1 = fu_udev_device_new(ctx, NULL);
	g_autoptr(FuUdevDevice) device2 = fu_udev_device_new(ctx, NULL);
	g_autoptr(FuUdevDevice) device3 = fu_udev_device_new(ctx, NULL);

	/* kernel netlink uses a name relative to devfs */
	fu_udev_device_add_property(device1, "DEVNAME", "hidraw1");
	g_assert_cmpstr(fu_udev_device_get_device_file(device1), ==, "/dev/hidraw1");

	/* ...which may include subdirectories */
	fu_udev_device_add_property(device2, "DEVNAME", "bus/usb/001/025");
	g_assert_cmpstr(fu_udev_device_get_device_file(device2), ==, "/dev/bus/usb/001/025");

	/* udev netlink uses an absolute path */
	fu_udev_device_add_property(device3, "DEVNAME", "/dev/hidraw1");
	g_assert_cmpstr(fu_udev_device_get_device_file(device3), ==, "/dev/hidraw1");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/udev-backend/strsplit-nul", fu_udev_backend_strsplit_nul_func);
	g_test_add_func("/fwupd/udev-backend/strsplit-nul/empty-values",
			fu_udev_backend_strsplit_nul_empty_values_func);
	g_test_add_func("/fwupd/udev-backend/strsplit-nul/unterminated",
			fu_udev_backend_strsplit_nul_unterminated_func);
	g_test_add_func("/fwupd/udev-backend/strsplit-nul/no-nul",
			fu_udev_backend_strsplit_nul_no_nul_func);
	g_test_add_func("/fwupd/udev-backend/strsplit-nul/invalid",
			fu_udev_backend_strsplit_nul_invalid_func);
	g_test_add_func("/fwupd/udev-backend/devname", fu_udev_backend_devname_func);
	return g_test_run();
}
