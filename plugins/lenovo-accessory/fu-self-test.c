/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lenovo-accessory-hid-child-device.h"

static void
fu_lenovo_accessory_hid_child_device_func(void)
{
	g_autoptr(FuLenovoAccessoryHidChildDevice) device =
	    fu_lenovo_accessory_hid_child_device_new(NULL);

	g_assert_nonnull(device);
	g_assert_cmpint(fu_lenovo_accessory_hid_child_device_get_pid(device), ==, 0x0);

	fu_lenovo_accessory_hid_child_device_set_pid(device, 0x1234);
	g_assert_cmpint(fu_lenovo_accessory_hid_child_device_get_pid(device), ==, 0x1234);

	fu_lenovo_accessory_hid_child_device_set_target_slot(device, 0x2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/lenovo-accessory/hid-child-device",
			fu_lenovo_accessory_hid_child_device_func);
	return g_test_run();
}
