/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-mm-device.h"

static void
fu_mm_device_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuMmDevice) mm_device = g_object_new(FU_TYPE_MM_DEVICE, "context", ctx, NULL);
	g_autoptr(GError) error = NULL;

	fu_device_set_physical_id(FU_DEVICE(mm_device), "/tmp");
	ret = fu_mm_device_set_autosuspend_delay(mm_device, 1500, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_mm_device_set_inhibited(mm_device, TRUE);
	g_assert_true(fu_mm_device_get_inhibited(mm_device));
	fu_mm_device_set_inhibited(mm_device, FALSE);
	g_assert_false(fu_mm_device_get_inhibited(mm_device));

	/* convert the instance IDs */
	fu_mm_device_add_instance_id(mm_device, "PCI\\SSVID_105B&SSPID_E142&REV_0000&CARRIER_CMCC");
	fu_mm_device_add_instance_id(mm_device, "PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC");

	/* show what we've got */
	str = fu_device_to_string(FU_DEVICE(mm_device));
	g_debug("%s", str);

	/* check it all makes sense */
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&SUBSYS_105BE142",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));

	/* add rev */
	fu_device_add_private_flag(FU_DEVICE(mm_device),
				   FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_mm_device_add_instance_id(mm_device, "PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC");
	fu_mm_device_add_instance_id(mm_device, "PCI\\SSVID_105B&SSPID_E142&REV_0000&CARRIER_CMCC");
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&REV_0000",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&SUBSYS_105BE142&REV_0000",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));

	/* add branch */
	fu_device_add_private_flag(FU_DEVICE(mm_device), FU_MM_DEVICE_FLAG_USE_BRANCH);
	fu_mm_device_add_instance_id(mm_device, "PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC");
	fu_mm_device_add_instance_id(mm_device, "PCI\\SSVID_105B&SSPID_E142&REV_0000&CARRIER_CMCC");
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(
	    FU_DEVICE(mm_device),
	    "PCI\\VID_17CB&PID_0308&SUBSYS_105BE142&REV_0000&CARRIER_CMCC",
	    FU_DEVICE_INSTANCE_FLAG_VISIBLE | FU_DEVICE_INSTANCE_FLAG_QUIRKS));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/mm/device", fu_mm_device_func);
	return g_test_run();
}
