/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-io-channel.h"
#include "fu-thelio-io-device.h"

struct _FuThelioIoDevice {
	FuUsbDevice			 parent_instance;
};

G_DEFINE_TYPE (FuThelioIoDevice, fu_thelio_io_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_thelio_io_device_probe (FuDevice *device, GError **error)
{
	const gchar *devpath;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *buf = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	fu_device_add_instance_id (device, "USB\\VID_03EB&PID_2FF4");

	/* convert GUsbDevice to GUdevDevice */
	udev_device = fu_usb_device_find_udev_device (FU_USB_DEVICE (device), error);
	if (udev_device == NULL)
		return FALSE;

	devpath = g_udev_device_get_sysfs_path (udev_device);
	if (G_UNLIKELY (devpath == NULL)) {
		g_set_error_literal (error,
			     FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Could not determine sysfs path for device");
		return FALSE;
	}

	fn = g_build_filename (devpath, "revision", NULL);
	if (!g_file_get_contents(fn, &buf, NULL, error))
		return FALSE;

	fu_device_set_version (device, (const gchar *) buf, FWUPD_VERSION_FORMAT_TRIPLET);

	return TRUE;
}

static gboolean
fu_thelio_io_device_detach (FuDevice *device, GError **error)
{
	const gchar *devpath;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;
	const guint8 buf[] = { '1', '\n' };

	/* convert GUsbDevice to GUdevDevice */
	udev_device = fu_usb_device_find_udev_device (FU_USB_DEVICE (device), error);
	if (udev_device == NULL)
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);

	devpath = g_udev_device_get_sysfs_path (udev_device);
	if (G_UNLIKELY (devpath == NULL)) {
		g_set_error_literal (error,
				 FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
				 "Could not determine sysfs path for device");
		return FALSE;
	}

	fn = g_build_filename (devpath, "bootloader", NULL);
	io_channel = fu_io_channel_new_file (fn, error);
	if (io_channel == NULL)
		return FALSE;
	if (!fu_io_channel_write_raw (io_channel, buf, sizeof(buf),
				      500, FU_IO_CHANNEL_FLAG_SINGLE_SHOT, error))
		return FALSE;
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_thelio_io_device_init (FuThelioIoDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_thelio_io_device_class_init (FuThelioIoDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_thelio_io_device_probe;
	klass_device->detach = fu_thelio_io_device_detach;
}
