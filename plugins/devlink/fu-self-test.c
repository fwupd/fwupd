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
	FuPathStore *pstore;
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
fu_devlink_netdevsim_sysfs_write(FuPathStore *pstore,
				 const gchar *filename,
				 const guint value,
				 GError **error)
{
	g_autofree gchar *path = NULL;

	path = fu_path_store_build_filename(pstore,
					    error,
					    FU_PATH_KIND_SYSFSDIR,
					    "bus",
					    "netdevsim",
					    filename,
					    NULL);
	if (path == NULL)
		return FALSE;
	return fu_devlink_file_write_helper(path, value, error);
}

static gboolean
fu_devlink_netdevsim_debugfs_write(FuPathStore *pstore,
				   const guint device_id,
				   const gchar *filename,
				   const guint value,
				   GError **error)
{
	g_autofree gchar *device_id_str = g_strdup_printf("netdevsim%u", device_id);
	g_autofree gchar *path = NULL;

	path = fu_path_store_build_filename(pstore,
					    error,
					    FU_PATH_KIND_DEBUGFSDIR,
					    "netdevsim",
					    device_id_str,
					    filename,
					    NULL);
	if (path == NULL)
		return FALSE;
	return fu_devlink_file_write_helper(path, value, error);
}

static void
fu_devlink_netdevsim_cleanup(FuDevlinkNetdevsim *ndsim)
{
	g_autoptr(GError) error_local = NULL;

	if (ndsim->pstore != NULL)
		g_object_unref(ndsim->pstore);
	if (ndsim->device_id != 0) {
		/* remove netdevsim device */
		if (!fu_devlink_netdevsim_sysfs_write(ndsim->pstore,
						      "del_device",
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
fu_devlink_netdevsim_new(FuPathStore *pstore, guint device_id, GError **error)
{
	g_autoptr(FuDevlinkNetdevsim) ndsim = g_new0(FuDevlinkNetdevsim, 1);
	g_autoptr(GError) error_local = NULL;

	/* create netdevsim device */
	if (!fu_devlink_netdevsim_sysfs_write(pstore, "new_device", device_id, error))
		return NULL;

	ndsim->device_id = device_id;
	ndsim->pstore = g_object_ref(pstore);

	if (!fu_devlink_netdevsim_debugfs_write(pstore,
						device_id,
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
	g_autoptr(FuDevlinkComponent) component = NULL;
	g_autoptr(FuDevlinkNetdevsim) ndsim = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw_data = NULL;
	g_autoptr(GError) error_local = NULL;

	/* an actual kernel device */
	fu_path_store_load_defaults(pstore);

	/* create test netdevsim to set up netdevsim device */
	ndsim = fu_devlink_netdevsim_new(pstore, FU_DEVLINK_NETDEVSIM_DEVICE_ID, &error_local);
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
	fu_device_add_child(device, FU_DEVICE(component));

	/* set component version for testing */
	fu_device_set_version(FU_DEVICE(component), "1.0.0");

	/* create firmware */
	fw_data = g_bytes_new(fw_content, strlen(fw_content));
	fu_firmware_set_bytes(firmware, fw_data);
	fu_firmware_set_version(firmware, "2.0.0");

	/* prepare the component */
	ret = fu_device_prepare(FU_DEVICE(component),
				progress,
				FWUPD_INSTALL_FLAG_NONE,
				&error_local);
	g_assert_true(ret);

	/* test firmware flashing on the fw.mgmt component */
	g_test_message("Testing firmware flash for fw.mgmt component on netdevsim/%s",
		       FU_DEVLINK_NETDEVSIM_DEVICE_NAME);
	ret = fu_device_write_firmware(FU_DEVICE(component),
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error_local);
	g_assert_true(ret);

	g_test_message("Firmware flash completed successfully for fw.mgmt component!");
	g_assert_cmpuint(fu_progress_get_percentage(progress), ==, 100);

	/* cleanup the component */
	ret = fu_device_cleanup(FU_DEVICE(component),
				progress,
				FWUPD_INSTALL_FLAG_NONE,
				&error_local);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/devlink/plugin/flash", fu_devlink_plugin_flash_func);
	return g_test_run();
}
