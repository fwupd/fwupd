/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static gboolean
fu_device_locker_open_cb(FuDevice *device, GError **error)
{
	g_assert_cmpstr(g_object_get_data(G_OBJECT(device), "state"), ==, "closed");
	g_object_set_data(G_OBJECT(device), "state", (gpointer) "opened");
	return TRUE;
}

static gboolean
fu_device_locker_close_cb(FuDevice *device, GError **error)
{
	g_assert_cmpstr(g_object_get_data(G_OBJECT(device), "state"), ==, "opened");
	g_object_set_data(G_OBJECT(device), "state", (gpointer) "closed-on-unref");
	return TRUE;
}

static void
fu_device_locker_func(void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	g_object_set_data(G_OBJECT(device), "state", (gpointer) "closed");
	locker = fu_device_locker_new_full(device,
					   fu_device_locker_open_cb,
					   fu_device_locker_close_cb,
					   &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	g_clear_object(&locker);
	g_assert_cmpstr(g_object_get_data(G_OBJECT(device), "state"), ==, "closed-on-unref");
}

static gboolean
fu_device_locker_fail_open_cb(FuDevice *device, GError **error)
{
	fu_device_set_metadata_boolean(device, "Test::Open", TRUE);
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "fail");
	return FALSE;
}

static gboolean
fu_device_locker_fail_close_cb(FuDevice *device, GError **error)
{
	fu_device_set_metadata_boolean(device, "Test::Close", TRUE);
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "busy");
	return FALSE;
}

static void
fu_device_locker_fail_func(void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_device_locker_fail_open_cb,
					   (FuDeviceLockerFunc)fu_device_locker_fail_close_cb,
					   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_null(locker);
	g_assert_true(fu_device_get_metadata_boolean(device, "Test::Open"));
	g_assert_true(fu_device_get_metadata_boolean(device, "Test::Close"));
	g_assert_false(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_IS_OPEN));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/device-locker", fu_device_locker_func);
	g_test_add_func("/fwupd/device-locker/fail", fu_device_locker_fail_func);
	return g_test_run();
}
