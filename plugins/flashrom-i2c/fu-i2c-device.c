/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "fu-i2c-device.h"

#include <glib.h>
#include <glib/gstdio.h>

#define LAYOUT_FLAG_NAME "FLAG"
#define LAYOUT_PARTITION_NAME "BLK"
#define IMG_LAYOUT_NAME "layout"
#define IMG_FLAG1_NAME "flag1.bin"
#define IMG_FLAG2_NAME "flag2.bin"
#define IMG_FIRMWARE_NAME "fw.bin"

struct FlashromArgs
{
	const gchar *spi_master;
	const gchar *layout;
	const gchar *image;
	const gchar *operation;
};

struct _FuI2cDevice
{
	FuDevice parent_instance;
};

G_DEFINE_TYPE (FuI2cDevice, fu_i2c_device, FU_TYPE_DEVICE)

/* This calls fu_common_get_contents_fd which will close the fd when done. */
static gint fu_i2c_device_get_boot_block_from_fd (int fd, GError **error)
{
	g_autoptr (GBytes) result = NULL;
	gsize result_len = 0;
	result = fu_common_get_contents_fd (fd, 1, error);
	if (result == NULL)
		return -1;

	const guint8 *result_bytes = g_bytes_get_data (result, &result_len);
	if (result_len != 1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s: bootblock info size is expected to be 1 byte, \
			     got %d",
			     __func__,
			     result_len);
		return -1;
	}

	return result_bytes[0];
}

static gboolean fu_i2c_device_validate_flashrom_args (
	const struct FlashromArgs *args,
	GError **error)
{
	if (args->spi_master == NULL ||
	    args->layout == NULL ||
	    args->image == NULL ||
	    args->operation == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s: all arguments under FlashromArgs has \
			     to be set",
			     __func__);
		return FALSE;
	}

	return TRUE;

}

static gboolean fu_i2c_device_run_command (const struct FlashromArgs *args,
					   GError **error)
{
	if (fu_i2c_device_validate_flashrom_args (args, error) == FALSE)
		return FALSE;

	const gchar *argv[9] = { "flashrom",
				 "-p", args->spi_master,
				 "--layout", args->layout,
				 "--image", args->image,
				 args->operation,
				 NULL };

	return fu_common_spawn_sync (argv, NULL, NULL, 0, NULL, error);
}

/* Update should happen on the non-activated block. If neither block is
 * activated, force update to block 1.
 */
static gint fu_i2c_device_get_target_block_no (
	const gchar *dir_name,
	const gchar *spi_master,
	const gchar *layout,
	const gchar *flag_name,
	GError **error)
{
	struct FlashromArgs flash_args;
	g_autofree gchar *img_arg = NULL;
	g_autofree gchar *tmp_file_name = NULL;
	tmp_file_name = g_build_filename (dir_name, "XXXXXX", NULL);
	gint fd = g_mkstemp (tmp_file_name);
	gint current_block = -1;

	if (fd == -1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s failed to create tmp file %s",
			     __func__,
			     tmp_file_name);
		return -1;
	}

	img_arg = g_strdup_printf ("%s:%s", flag_name, tmp_file_name);
	flash_args.spi_master = spi_master;
	flash_args.layout = layout;
	flash_args.image = img_arg;
	flash_args.operation = "-r";

	if (fu_i2c_device_run_command (&flash_args, error) == TRUE) {
		current_block = fu_i2c_device_get_boot_block_from_fd (
			fd, error);
	} else {
		g_autoptr(GError) error_local = NULL;
		if (g_close (fd, &error_local) == FALSE) {
			g_warning ("%s failed to close fd on %s: %s",
				   __func__,
				   tmp_file_name,
				   error_local->message);
		}

		fd = -1;
	}

	g_unlink (tmp_file_name);

	if (current_block == -1)
		return -1;
	else if (current_block == 1)
		return 2;
	else
		return 1;
}

static gboolean fu_i2c_file_readable (const gchar *path, GError **error)
{
	gboolean result = g_access (path, R_OK);
	if (result != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s failed to access file %s",
			     __func__,
			     path);
	}

	return (result == 0);
}

static gboolean fu_i2c_device_write_firmware (FuDevice *device,
					      FuFirmware *firmware,
					      FwupdInstallFlags flags,
					      GError **error)
{
	gint block_no;
	gint bus_no = fu_device_get_metadata_integer (device, PORT_NAME);
	struct FlashromArgs flash_args_write_fw;
	struct FlashromArgs flash_args_write_flg;
	const gchar *programmer_name = fu_device_get_metadata (device,
		PROGRAMMER_NAME);
	g_autoptr (GBytes) archive_bytes = NULL;
	g_autofree gchar *tmp_dir_name = NULL;
	g_autofree gchar *flash_spi_master_arg = NULL;
	g_autofree gchar *flash_write_fw_image_arg = NULL;
	g_autofree gchar *flash_write_flg_image_arg = NULL;
	g_autofree gchar *partition_name = NULL;
	g_autofree gchar *layout_file_path = NULL;
	g_autofree gchar *flag1_file_path = NULL;
	g_autofree gchar *flag2_file_path = NULL;
	g_autofree gchar *firmware_file_path = NULL;
	g_autofree gchar *flag_file_name = NULL;
	gboolean ret = TRUE;
	archive_bytes = fu_firmware_get_image_default_bytes (firmware, error);
	tmp_dir_name = g_strdup_printf ("/tmp/flashrom-i2c-%d-XXXXXX", bus_no);
	if (g_mkdtemp (tmp_dir_name) == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s failed to create tmp dir %s",
			     __func__,
			     tmp_dir_name);
		ret = FALSE;
		goto cleanup;
	}

	ret = fu_common_extract_archive (archive_bytes, tmp_dir_name, error);
	if (ret == FALSE) {
		ret = FALSE;
		goto cleanup;
	}

	layout_file_path = g_build_filename (
		tmp_dir_name,
		IMG_LAYOUT_NAME,
		NULL);
	flag1_file_path = g_build_filename (
		tmp_dir_name,
		IMG_FLAG1_NAME,
		NULL);
	flag2_file_path = g_build_filename (
		tmp_dir_name,
		IMG_FLAG2_NAME,
		NULL);
	firmware_file_path = g_build_filename (
		tmp_dir_name,
		IMG_FIRMWARE_NAME,
		NULL);

	if (fu_i2c_file_readable (layout_file_path, error) == FALSE ||
	    fu_i2c_file_readable (flag1_file_path, error) == FALSE ||
	    fu_i2c_file_readable (flag2_file_path, error) == FALSE ||
	    fu_i2c_file_readable (firmware_file_path, error) == FALSE) {
		ret = FALSE;
		goto cleanup;
	}

	flash_spi_master_arg = g_strdup_printf ("%s:bus=%d",
		programmer_name, bus_no);
	block_no = fu_i2c_device_get_target_block_no (
		tmp_dir_name, flash_spi_master_arg, layout_file_path,
		LAYOUT_FLAG_NAME, error);
	if (block_no == -1) {
		ret = FALSE;
		goto cleanup;
	}

	partition_name = g_strdup_printf (
		"%s%d", LAYOUT_PARTITION_NAME, block_no);
	if (block_no == 1)
		flag_file_name = g_strdup (flag1_file_path);
	else
		flag_file_name = g_strdup (flag2_file_path);

	flash_write_fw_image_arg = g_strdup_printf ("%s:%s",
						   partition_name,
						   firmware_file_path);
	flash_write_flg_image_arg = g_strdup_printf ("%s:%s",
						    partition_name,
						    firmware_file_path);

	flash_args_write_fw.spi_master = flash_spi_master_arg;
	flash_args_write_fw.layout = layout_file_path;
	flash_args_write_fw.image = flash_write_fw_image_arg;
	flash_args_write_fw.operation = "-w";
	flash_args_write_flg.spi_master = flash_spi_master_arg;
	flash_args_write_flg.layout = layout_file_path;
	flash_args_write_flg.image = flash_write_flg_image_arg;
	flash_args_write_flg.operation = "-w";

	ret = fu_i2c_device_run_command (&flash_args_write_fw, error);
	if (ret == TRUE)
		ret = fu_i2c_device_run_command (&flash_args_write_flg, error);

cleanup:
	fu_common_rmtree (tmp_dir_name, NULL);
	return ret;
}

static void fu_i2c_device_init (FuI2cDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
}

static void fu_i2c_device_class_init (FuI2cDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_i2c_device_write_firmware;
}
