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

typedef struct {
	guint device_id;
} FuDevlinkNetdevsim;

static gboolean
fu_devlink_file_write_helper(const gchar *path, const guint value, GError **error)
{
	g_autofree gchar *value_str = g_strdup_printf("%u", value);
	g_autoptr(FuIOChannel) io = NULL;

	/* check if file exists first */
	if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "file not found: %s", path);
		return FALSE;
	}

	io = fu_io_channel_new_file(path, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io == NULL)
		return FALSE;

	return fu_io_channel_write_raw(io,
				       (const guint8 *)value_str,
				       strlen(value_str),
				       1000,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

static gboolean
fu_devlink_netdevsim_sysfs_write(const gchar *filename, const guint value, GError **error)
{
	g_autofree gchar *path = g_build_filename(fu_path_from_kind(FU_PATH_KIND_SYSFSDIR),
						  "bus",
						  "netdevsim",
						  filename,
						  NULL);

	return fu_devlink_file_write_helper(path, value, error);
}

static gboolean
fu_devlink_netdevsim_debugfs_write(const guint device_id,
				   const gchar *filename,
				   const guint value,
				   GError **error)
{
	g_autofree gchar *device_id_str = g_strdup_printf("netdevsim%u", device_id);
	g_autofree gchar *path = g_build_filename(fu_path_from_kind(FU_PATH_KIND_DEBUGFSDIR),
						  "netdevsim",
						  device_id_str,
						  filename,
						  NULL);

	return fu_devlink_file_write_helper(path, value, error);
}

static void
fu_devlink_netdevsim_cleanup(FuDevlinkNetdevsim *ndsim)
{
	g_autoptr(GError) local_error = NULL;

	if (ndsim->device_id == 0)
		return;

	/* remove netdevsim device */
	if (!fu_devlink_netdevsim_sysfs_write("del_device", ndsim->device_id, &local_error)) {
		g_debug("Failed to remove netdevsim device %u: %s",
			ndsim->device_id,
			local_error->message);
	}
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDevlinkNetdevsim, fu_devlink_netdevsim_cleanup)

#define FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_SIZE	   0x100
#define FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_CHUNK_SIZE	   0x10
#define FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_CHUNK_TIME_MS 0x10

static FuDevlinkNetdevsim *
fu_devlink_netdevsim_new(guint device_id, GError **error)
{
	g_autoptr(FuDevlinkNetdevsim) ndsim = g_new0(FuDevlinkNetdevsim, 1);
	g_autoptr(GError) local_error = NULL;

	/* create netdevsim device */
	if (!fu_devlink_netdevsim_sysfs_write("new_device", device_id, error))
		return NULL;
	ndsim->device_id = device_id;

	/* set the flash time to 100ms by reducing flash size, chunk size and chunk time */
	if (!fu_devlink_netdevsim_debugfs_write(device_id,
						"fw_update_flash_size",
						FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_SIZE,
						&local_error)) {
		g_debug("Failed to write fw_update_flash_size: %s", local_error->message);
		g_clear_error(&local_error);
	}
	if (!fu_devlink_netdevsim_debugfs_write(device_id,
						"fw_update_flash_chunk_size",
						FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_CHUNK_SIZE,
						&local_error)) {
		g_debug("Failed to write fw_update_flash_chunk_size: %s", local_error->message);
		g_clear_error(&local_error);
	}
	if (!fu_devlink_netdevsim_debugfs_write(device_id,
						"fw_update_flash_chunk_time_ms",
						FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_CHUNK_TIME_MS,
						&local_error)) {
		g_debug("Failed to write fw_update_flash_chunk_time_ms: %s", local_error->message);
		g_clear_error(&local_error);
	}

	return g_steal_pointer(&ndsim);
}

#define FU_DEVLINK_NETDEVSIM_DEVICE_ID	 472187
#define FU_DEVLINK_NETDEVSIM_DEVICE_NAME "netdevsim" G_STRINGIFY(FU_DEVLINK_NETDEVSIM_DEVICE_ID)

static void
fu_devlink_netdevsim_device_func(void)
{
	gboolean ret;
	g_autoptr(FuDevlinkDevice) device = NULL;
	g_autoptr(FuDevlinkNetdevsim) ndsim = NULL;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(GError) local_error = NULL;

	/* create test netdevsim to set up netdevsim device */
	ndsim = fu_devlink_netdevsim_new(FU_DEVLINK_NETDEVSIM_DEVICE_ID, &local_error);
	if (ndsim == NULL) {
		g_test_skip_printf("Failed to create netdevsim device: %s", local_error->message);
		return;
	}

	/* create context */
	ctx = fu_context_new();

	/* create device with valid bus and device names */
	device = fu_devlink_device_new(ctx, "netdevsim", FU_DEVLINK_NETDEVSIM_DEVICE_NAME);
	g_assert_nonnull(device);

	ret = fu_device_probe(FU_DEVICE(device), &local_error);
	g_assert_true(ret);

	/* check device properties were set correctly */
	g_assert_cmpstr(fu_device_get_summary(FU_DEVICE(device)), ==, "Devlink device");
}

static void
fu_devlink_netdevsim_device_flash_func(void)
{
	gboolean ret;
	const gchar *fw_content = "FWUPD_TEST_FIRMWARE_v2.0.0\nTest firmware for devlink device";
	g_autoptr(FuDevlinkDevice) device = NULL;
	g_autoptr(FuDevice) component = NULL;
	g_autoptr(FuDevlinkNetdevsim) ndsim = NULL;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = NULL;
	g_autoptr(GBytes) fw_data = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *instance_id = NULL;

	/* create test netdevsim to set up netdevsim device */
	ndsim = fu_devlink_netdevsim_new(FU_DEVLINK_NETDEVSIM_DEVICE_ID, &local_error);
	if (ndsim == NULL) {
		g_test_skip_printf("Failed to create netdevsim device: %s", local_error->message);
		return;
	}

	/* create context */
	ctx = fu_context_new();

	/* create device with valid bus and device names */
	device = fu_devlink_device_new(ctx, "netdevsim", FU_DEVLINK_NETDEVSIM_DEVICE_NAME);
	g_assert_nonnull(device);

	/* probe device first */
	ret = fu_device_probe(FU_DEVICE(device), &local_error);
	g_assert_true(ret);

	/* create fw.mgmt component */
	instance_id = g_strdup_printf("DEVLINK\\BUS_netdevsim&DEV_%s&COMPONENT_fw.mgmt",
				      FU_DEVLINK_NETDEVSIM_DEVICE_NAME);
	component = fu_devlink_component_new(ctx, instance_id, "fw.mgmt");
	g_assert_nonnull(component);

	/* set up parent-child relationship */
	fu_device_add_child(FU_DEVICE(device), component);

	/* set component version for testing */
	fu_device_set_version(component, "1.0.0");

	/* create firmware */
	firmware = fu_firmware_new();
	fw_data = g_bytes_new(fw_content, strlen(fw_content));
	fu_firmware_set_bytes(firmware, fw_data);
	fu_firmware_set_version(firmware, "2.0.0");

	/* create progress tracker */
	progress = fu_progress_new(G_STRLOC);

	/* prepare the component */
	ret = fu_device_prepare(component, progress, FWUPD_INSTALL_FLAG_NONE, &local_error);
	g_assert_true(ret);

	/* test firmware flashing on the fw.mgmt component */
	g_test_message("Testing firmware flash for fw.mgmt component on netdevsim/%s",
		       FU_DEVLINK_NETDEVSIM_DEVICE_NAME);
	ret = fu_device_write_firmware(component,
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &local_error);
	g_assert_true(ret);

	g_test_message("Firmware flash completed successfully for fw.mgmt component!");
	g_assert_cmpuint(fu_progress_get_percentage(progress), ==, 100);

	/* cleanup the component */
	ret = fu_device_cleanup(component, progress, FWUPD_INSTALL_FLAG_NONE, &local_error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func("/devlink/netdevsim/device", fu_devlink_netdevsim_device_func);
	g_test_add_func("/devlink/netdevsim/flash", fu_devlink_netdevsim_device_flash_func);
	return g_test_run();
}
