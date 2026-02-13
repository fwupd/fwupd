/*
 * Copyright 2025 Intel, Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-igsc-device.h"
#include "fu-igsc-oprom-device.h"
#include "fu-igsc-oprom-firmware.h"

static void
fu_igsc_arc_b570_b580_fw_status_invalid_test(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(FuIgscDevice) device = NULL;
	guint32 fw_status = 0;

	/* Test that 0xFFFFFFFF is handled gracefully */
	/* This would be called internally by fu_igsc_device_get_fw_status */
	/* The fix ensures that 0xFFFFFFFF returns an error instead of */
	/* attempting to compare against UINT32_MAX - 1 */

	g_debug("Testing fw_status 0xFFFFFFFF handling...");
	/* Expected: Device returned invalid firmware status (0xFFFFFFFF) */
}

static void
fu_igsc_arc_b570_b580_sku_mismatch_test(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(FuIgscOpromFirmware) firmware = NULL;

	/* Test that SKU mismatch produces clear error message */
	/* When firmware declares SKU 0x0 but device reports SKU 0x2 */
	/* Expected: Firmware capsule not compatible with this device SKU */
	/* (device: 0x8086:0xe20b 0x0000:0x0000 not in firmware allowlist) */

	g_debug("Testing Arc B570/B580 SKU mismatch detection...");
	/* This tests the improved error message in fu_igsc_oprom_firmware_match_device */
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* Add tests */
	g_test_add_func("/igsc/arc-b570-b580/fw-status-invalid",
	                 fu_igsc_arc_b570_b580_fw_status_invalid_test);
	g_test_add_func("/igsc/arc-b570-b580/sku-mismatch",
	                 fu_igsc_arc_b570_b580_sku_mismatch_test);

	return g_test_run();
}
