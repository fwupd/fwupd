/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-devlink-component.h"
#include "fu-devlink-device.h"
#include "fu-devlink-plugin.h"
#include "fu-plugin-private.h"

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
	g_autofree gchar *sysfs_dir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR);
	g_autofree gchar *path = g_build_filename(sysfs_dir, "bus", "netdevsim", filename, NULL);

	return fu_devlink_file_write_helper(path, value, error);
}

static gboolean
fu_devlink_netdevsim_debugfs_write(const guint device_id,
				   const gchar *filename,
				   const guint value,
				   GError **error)
{
	g_autofree gchar *device_id_str = g_strdup_printf("netdevsim%u", device_id);
	g_autofree gchar *debugfs_dir = fu_path_from_kind(FU_PATH_KIND_DEBUGFSDIR);
	g_autofree gchar *path =
	    g_build_filename(debugfs_dir, "netdevsim", device_id_str, filename, NULL);

	return fu_devlink_file_write_helper(path, value, error);
}

static void
fu_devlink_netdevsim_cleanup(FuDevlinkNetdevsim *ndsim)
{
	g_autoptr(GError) error_local = NULL;

	if (ndsim->device_id != 0) {
		/* remove netdevsim device */
		if (!fu_devlink_netdevsim_sysfs_write("del_device",
						      ndsim->device_id,
						      &error_local)) {
			g_debug("Failed to remove netdevsim device %u: %s",
				ndsim->device_id,
				error_local->message);
		}
	}
	g_free(ndsim);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDevlinkNetdevsim, fu_devlink_netdevsim_cleanup)

#define FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_CHUNK_TIME_MS 1 /* 1ms */

static FuDevlinkNetdevsim *
fu_devlink_netdevsim_new(guint device_id, GError **error)
{
	g_autoptr(FuDevlinkNetdevsim) ndsim = g_new0(FuDevlinkNetdevsim, 1);
	g_autoptr(GError) error_local = NULL;

	/* create netdevsim device */
	if (!fu_devlink_netdevsim_sysfs_write("new_device", device_id, error))
		return NULL;

	ndsim->device_id = device_id;

	if (!fu_devlink_netdevsim_debugfs_write(device_id,
						"fw_update_flash_chunk_time_ms",
						FU_DEVLINK_NETDEVSIM_FW_UPDATE_FLASH_CHUNK_TIME_MS,
						&error_local)) {
		g_debug("Failed to write fw_update_flash_chunk_time_ms: %s", error_local->message);
		g_clear_error(&error_local);
	}

	return g_steal_pointer(&ndsim);
}

#define FU_DEVLINK_NETDEVSIM_DEVICE_ID	 472187
#define FU_DEVLINK_NETDEVSIM_DEVICE_NAME "netdevsim" G_STRINGIFY(FU_DEVLINK_NETDEVSIM_DEVICE_ID)

static void
fu_devlink_plugin_flash_func(void)
{
	gboolean ret;
	const gchar *fw_content = "FWUPD_TEST_FIRMWARE_v2.0.0\nTest firmware for devlink device";
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) component = NULL;
	g_autoptr(FuDevlinkNetdevsim) ndsim = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw_data = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *instance_id = NULL;

	/* create test netdevsim to set up netdevsim device */
	ndsim = fu_devlink_netdevsim_new(FU_DEVLINK_NETDEVSIM_DEVICE_ID, &error_local);
	if (ndsim == NULL) {
		g_autofree gchar *msg =
		    g_strdup_printf("failed to create netdevsim device: %s", error_local->message);
		g_test_skip(msg);
		return;
	}

	/* create device with valid bus and device names */
	device = fu_devlink_device_new(ctx, "netdevsim", FU_DEVLINK_NETDEVSIM_DEVICE_NAME, NULL);
	g_assert_nonnull(device);

	/* probe device first */
	ret = fu_device_probe(device, &error_local);
	g_assert_true(ret);

	/* open device */
	ret = fu_device_open(device, &error_local);

	g_assert_true(ret);

	/* create fw.mgmt component */
	component = fu_devlink_component_new(device, "fw.mgmt");
	g_assert_nonnull(component);

	/* set up parent-child relationship */
	fu_device_add_child(device, component);

	/* set component version for testing */
	fu_device_set_version(component, "1.0.0");

	/* create firmware */
	fw_data = g_bytes_new(fw_content, strlen(fw_content));
	fu_firmware_set_bytes(firmware, fw_data);
	fu_firmware_set_version(firmware, "2.0.0");

	/* prepare the component */
	ret = fu_device_prepare(component, progress, FWUPD_INSTALL_FLAG_NONE, &error_local);
	g_assert_true(ret);

	/* test firmware flashing on the fw.mgmt component */
	g_test_message("Testing firmware flash for fw.mgmt component on netdevsim/%s",
		       FU_DEVLINK_NETDEVSIM_DEVICE_NAME);
	ret = fu_device_write_firmware(component,
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error_local);
	g_assert_true(ret);

	g_test_message("Firmware flash completed successfully for fw.mgmt component!");
	g_assert_cmpuint(fu_progress_get_percentage(progress), ==, 100);

	/* cleanup the component */
	ret = fu_device_cleanup(component, progress, FWUPD_INSTALL_FLAG_NONE, &error_local);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func("/devlink/plugin/flash", fu_devlink_plugin_flash_func);
	return g_test_run();
}
