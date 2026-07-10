/*
 * Copyright 2026 HongXing Xu <qifeng.liu@rollingwireless.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-fastboot-rolling-device.h"

struct _FuFastbootRollingDevice {
	FuFastbootDevice parent_instance;
};

G_DEFINE_TYPE(FuFastbootRollingDevice, fu_fastboot_rolling_device, FU_TYPE_FASTBOOT_DEVICE)

typedef struct {
	gchar *check_path;
	guint consecutive;
} FuFastbootRollingDeviceWaitHelper;

static gboolean
fu_fastboot_rolling_device_wait_interface_cb(FuDevice *device,
					     gpointer user_data,
					     GError **error)
{
	FuFastbootRollingDeviceWaitHelper *helper = user_data;

	if (g_file_test(helper->check_path, G_FILE_TEST_EXISTS)) {
		helper->consecutive++;
		if (helper->consecutive >= 3) {
			g_info("interface directory exists continuously for 3 seconds");
			return TRUE;
		}
	} else {
		helper->consecutive = 0;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "interface directory not present");
	return FALSE;
}

static gboolean
fu_fastboot_rolling_device_rebind_cdc_mbim(FuFastbootRollingDevice *self, GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuFastbootRollingDeviceWaitHelper helper = {0};
	const gchar *sysfs_path = NULL;
	const gchar *bind_id = NULL;
	g_autofree gchar *bind_str = NULL;
	g_autofree gchar *bind_file_path = NULL;
	g_autofree gchar *check_path = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;

	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	if (sysfs_path == NULL || sysfs_path[0] == '\0') {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device has no sysfs path");
		return FALSE;
	}
	bind_id = fu_udev_device_get_bind_id(FU_UDEV_DEVICE(self));
	if (bind_id == NULL || bind_id[0] == '\0') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to get bind ID from sysfs path: %s",
			    sysfs_path);
		return FALSE;
	}

	bind_str = g_strdup_printf("%s:1.0", bind_id);
	check_path = g_build_filename(sysfs_path, bind_str, NULL);
	helper.check_path = check_path;

	/* wait for the interface directory to appear (max 10 tries, 1s each)
	 * and remain for 3 seconds */
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_fastboot_rolling_device_wait_interface_cb,
			     10,
			     &helper,
			     error))
		return FALSE;

	bind_file_path = fu_context_build_filename(ctx,
						   error,
						   FU_PATH_KIND_SYSFSDIR,
						   "bus",
						   "usb",
						   "drivers",
						   "cdc_mbim",
						   "bind",
						   NULL);
	if (bind_file_path == NULL)
		return FALSE;
	io_channel = fu_io_channel_new_file(bind_file_path, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io_channel == NULL)
		return FALSE;
	if (!fu_io_channel_write_raw(io_channel,
				     (const guint8 *)bind_str,
				     strlen(bind_str),
				     1000,
				     FU_IO_CHANNEL_FLAG_NONE,
				     error))
		return FALSE;

	/* success */
	g_info("successfully rebound USB device %s to cdc_mbim", bind_str);
	return TRUE;
}

static gboolean
fu_fastboot_rolling_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFastbootRollingDevice *self = FU_FASTBOOT_ROLLING_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	if (!FU_DEVICE_CLASS(fu_fastboot_rolling_device_parent_class)->attach(device, progress, error))
		return FALSE;

	if (!fu_fastboot_rolling_device_rebind_cdc_mbim(self, &error_local))
		g_warning("failed to rebind cdc_mbim: %s", error_local->message);

	/* success */
	return TRUE;
}

static void
fu_fastboot_rolling_device_init(FuFastbootRollingDevice *self)
{
}

static void
fu_fastboot_rolling_device_class_init(FuFastbootRollingDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_fastboot_rolling_device_attach;
}
