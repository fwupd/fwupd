/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-devlink-component.h"
#include "fu-devlink-device.h"

static void
fu_devlink_device_func(void)
{
	g_autoptr(FuDevlinkDevice) device = NULL;
	g_autoptr(FuContext) ctx = NULL;
	gboolean ret;
	g_autoptr(GError) error = NULL;

	/* Create context */
	ctx = fu_context_new();

	/* Create device with valid bus and device names (no socket for testing) */
	device = fu_devlink_device_new(ctx, "netdevsim", "netdevsim1");
	g_assert_nonnull(device);

	/* Set context */
	fu_device_set_context(FU_DEVICE(device), ctx);

	ret = fu_device_probe(FU_DEVICE(device), &error);
	if (!ret) {
		g_test_message("Device probe failed (expected if devlink not available): %s",
			       error->message);
		/* Don't fail the test if devlink isn't available */
		return;
	}
	g_assert_true(ret);

	/* Check device properties were set correctly */
	g_assert_cmpstr(fu_device_get_summary(FU_DEVICE(device)), ==, "Devlink device");
}

static void
fu_devlink_device_flash_func(void)
{
	g_autoptr(FuDevlinkDevice) device = NULL;
	g_autoptr(FuDevice) component = NULL;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = NULL;
	g_autoptr(GBytes) fw_data = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *instance_id = NULL;
	gboolean ret;
	const gchar *fw_content = "FWUPD_TEST_FIRMWARE_v2.0.0\nTest firmware for devlink device";

	/* Create context */
	ctx = fu_context_new();

	/* Create device (no socket for testing) */
	device = fu_devlink_device_new(ctx, "netdevsim", "netdevsim1");
	g_assert_nonnull(device);

	/* Set context */
	fu_device_set_context(FU_DEVICE(device), ctx);

	/* Probe device first */
	ret = fu_device_probe(FU_DEVICE(device), &error);
	if (!ret) {
		g_test_message("Device probe failed (expected if devlink not available): %s",
			       error->message);
		/* Don't fail the test if devlink isn't available */
		return;
	}

	/* Create fw.mgmt component */
	instance_id = g_strdup_printf("DEVLINK\\BUS_netdevsim&DEV_netdevsim1&COMPONENT_fw.mgmt");
	component = fu_devlink_component_new(ctx, instance_id, "fw.mgmt");
	g_assert_nonnull(component);

	/* Set up parent-child relationship */
	fu_device_add_child(FU_DEVICE(device), component);

	/* Set component version for testing */
	fu_device_set_version(component, "1.0.0");

	/* Create firmware */
	firmware = fu_firmware_new();
	fw_data = g_bytes_new(fw_content, strlen(fw_content));
	fu_firmware_set_bytes(firmware, fw_data);
	fu_firmware_set_version(firmware, "2.0.0");

	/* Create progress tracker */
	progress = fu_progress_new(G_STRLOC);

	/* Test firmware flashing on the fw.mgmt component */
	g_test_message("Testing firmware flash for fw.mgmt component on netdevsim/netdevsim1");
	ret = fu_device_write_firmware(component,
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);

	if (!ret) {
		g_test_message("Firmware flash failed (expected if netdevsim not available): %s",
			       error->message);
		/* Don't fail the test if netdevsim isn't available */
		return;
	}

	g_test_message("Firmware flash completed successfully for fw.mgmt component!");
	g_assert_cmpuint(fu_progress_get_percentage(progress), ==, 100);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func("/devlink/device", fu_devlink_device_func);
	g_test_add_func("/devlink/flash", fu_devlink_device_flash_func);
	return g_test_run();
}
