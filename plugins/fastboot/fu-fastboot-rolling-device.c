/*
 * Copyright 2026 HongXing Xu <qifeng.liu@rollingwireless.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-fastboot-rolling-device.h"

#define FASTBOOT_REMOVE_DELAY_RE_ENUMERATE 60000 /* ms */

struct _FuFastbootRollingDevice {
	FuFastbootDevice parent_instance;
};

G_DEFINE_TYPE(FuFastbootRollingDevice, fu_fastboot_rolling_device, FU_TYPE_FASTBOOT_DEVICE)

static gboolean
fu_fastboot_rolling_device_rebind_cdc_mbim(FuFastbootRollingDevice *self, FuProgress *progress)
{
	const gchar *sysfs_path = NULL;
	g_autofree gchar *bind_id = NULL;
	g_autofree gchar *bind_str = NULL;
	g_autofree gchar *bind_file_path = NULL;
	g_autofree gchar *check_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;
	const gint max_tries = 10;
	gint consecutive = 0;
	gint tries = 0;

	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	if (sysfs_path == NULL || *sysfs_path == '\0') {
		g_warning("device has no sysfs path");
		return FALSE;
	}
	bind_id = g_path_get_basename(sysfs_path);
	if (bind_id == NULL || *bind_id == '\0') {
		g_warning("failed to get basename from sysfs path: %s", sysfs_path);
		return FALSE;
	}

	bind_str = g_strdup_printf("%s:1.0", bind_id);

	check_path = g_build_filename(sysfs_path, bind_str, NULL);
	g_info("checking for existence of: %s", check_path);

	/* wait for the interface directory to appear (max 10 tries, 1s each)
	 * and remain for 3 seconds */
	while (tries < max_tries) {
		if (g_file_test(check_path, G_FILE_TEST_EXISTS)) {
			consecutive++;
			if (consecutive >= 3) {
				g_info("interface directory exists continuously for 3 seconds");
				break;
			}
		} else {
			consecutive = 0;
		}

		tries++;
		fu_device_sleep_full(FU_DEVICE(self), 1000, progress);
	}

	if (tries >= max_tries && consecutive < 3) {
		g_warning("timeout waiting for interface directory: %s", check_path);
		return FALSE;
	}

	bind_file_path = g_build_filename("/sys/bus/usb/drivers/cdc_mbim", "bind", NULL);
	io_channel = fu_io_channel_new_file(bind_file_path, FU_IO_CHANNEL_OPEN_FLAG_WRITE, &error);
	if (io_channel == NULL) {
		g_warning("failed to open bind file %s: %s", bind_file_path, error->message);
		return FALSE;
	}

	if (!fu_io_channel_write_raw(io_channel,
				     (const guint8 *)bind_str,
				     strlen(bind_str),
				     1000,
				     FU_IO_CHANNEL_FLAG_NONE,
				     &error)) {
		g_warning("failed to write to bind file %s: %s", bind_file_path, error->message);
		return FALSE;
	}

	g_info("successfully rebound USB device %s to cdc_mbim", bind_str);
	return TRUE;
}

static gboolean
fu_fastboot_rolling_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFastbootRollingDevice *self = FU_FASTBOOT_ROLLING_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_fastboot_rolling_device_parent_class)->attach(device, progress, error))
		return FALSE;

	if (!fu_fastboot_rolling_device_rebind_cdc_mbim(self, progress)) {
		g_warning("rebind cdc_mbim fail");
	}

	return TRUE;
}

static void
fu_fastboot_rolling_device_init(FuFastbootRollingDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.google.fastboot");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_remove_delay(FU_DEVICE(self), FASTBOOT_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ZIP_FIRMWARE);
	fu_usb_device_set_claim_retry_count(FU_USB_DEVICE(self), 5);
}

static void
fu_fastboot_rolling_device_class_init(FuFastbootRollingDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_fastboot_rolling_device_attach;
}
