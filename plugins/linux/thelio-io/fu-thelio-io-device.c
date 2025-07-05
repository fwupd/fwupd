/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-thelio-io-device.h"

struct _FuThelioIoDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuThelioIoDevice, fu_thelio_io_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_thelio_io_device_probe(FuDevice *device, GError **error)
{
	const gchar *devpath;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *buf = NULL;
	g_autoptr(GError) error_local = NULL;

	/* this is the atmel bootloader */
	fu_device_add_instance_id_full(device,
				       "USB\\VID_03EB&PID_2FF4",
				       FU_DEVICE_INSTANCE_FLAG_COUNTERPART);

	devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (G_UNLIKELY(devpath == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Could not determine sysfs path for device");
		return FALSE;
	}

	/* pre-1.0.0 firmware versions do not implement this */
	fn = g_build_filename(devpath, "revision", NULL);
	if (!g_file_get_contents(fn, &buf, &bufsz, &error_local)) {
		if (g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_FAILED)) {
			g_debug("FW revision unimplemented: %s", error_local->message);
			fu_device_set_version(device, "0.0.0");
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	} else {
		g_autofree gchar *version = fu_strsafe((const gchar *)buf, bufsz);
		fu_device_set_version(device, version);
	}

	return TRUE;
}

static gboolean
fu_thelio_io_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	const gchar *devpath;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;
	const guint8 buf[] = {'1', '\n'};

	devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (G_UNLIKELY(devpath == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Could not determine sysfs path for device");
		return FALSE;
	}

	fn = g_build_filename(devpath, "bootloader", NULL);
	io_channel = fu_io_channel_new_file(fn, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io_channel == NULL)
		return FALSE;
	if (!fu_io_channel_write_raw(io_channel,
				     buf,
				     sizeof(buf),
				     500,
				     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				     error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_thelio_io_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_thelio_io_device_init(FuThelioIoDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_protocol(FU_DEVICE(self), "org.usb.dfu");
}

static void
fu_thelio_io_device_class_init(FuThelioIoDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_thelio_io_device_probe;
	device_class->detach = fu_thelio_io_device_detach;
	device_class->set_progress = fu_thelio_io_device_set_progress;
}
