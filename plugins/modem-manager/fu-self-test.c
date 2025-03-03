/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include "fu-mm-device.h"

static void
fu_mm_device_func(void)
{
	gboolean ret;
	g_autoptr(FuMmDevice) mm_device = g_object_new(FU_TYPE_MM_DEVICE, NULL);
	g_autoptr(GError) error = NULL;

	fu_device_set_physical_id(FU_DEVICE(mm_device), "/tmp");
	ret = fu_mm_device_set_autosuspend_delay(mm_device, 1500, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_mm_device_set_inhibited(mm_device, TRUE);
	g_assert_true(fu_mm_device_get_inhibited(mm_device));
	fu_mm_device_set_inhibited(mm_device, FALSE);
	g_assert_false(fu_mm_device_get_inhibited(mm_device));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/mm/device", fu_mm_device_func);
	return g_test_run();
}
