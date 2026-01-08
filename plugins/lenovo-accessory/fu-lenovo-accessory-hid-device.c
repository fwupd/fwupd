/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-common.h"
#include "fu-lenovo-accessory-hid-device.h"

struct _FuLenovoAccessoryHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLenovoAccessoryHidDevice, fu_lenovo_accessory_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_LENOVO_HID_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_lenovo_accessory_hid_device_setup(FuDevice *device, GError **error)
{
	g_autoptr(FuHidDescriptor) udev_des = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(FuDevice) usb_parent = NULL;
	g_autofree gchar *version = NULL;
	guint8 major = 0;
	guint8 minor = 0;
	guint8 internal = 0;
	guint16 pid = 0;
	FuHidrawDevice *udev_device = FU_HIDRAW_DEVICE(device);
	udev_des = fu_hidraw_device_parse_descriptor(udev_device, error);
	if (udev_des == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(udev_des,
					       error,
					       "usage-page",
					       0xFF00,
					       "usage",
					       0x02,
					       "report-size",
					       8,
					       "report-count",
					       0x40,
					       NULL);
	if (report == NULL)
		return FALSE;
	usb_parent = fu_device_get_backend_parent_with_subsystem(device, "usb:usb_device", error);
	if (usb_parent == NULL)
		return FALSE;
	if (!fu_device_probe(usb_parent, error))
		return FALSE;
	fu_device_set_version_raw(device, fu_usb_device_get_release(FU_USB_DEVICE(usb_parent)));
	if (!fu_lenovo_accessory_hid_fwversion(FU_HIDRAW_DEVICE(device),
					       &major,
					       &minor,
					       &internal,
					       error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, internal);
	fu_device_set_version(device, version);
	pid = fu_device_get_pid(device);
	if (pid == 0x6201) {
		fu_device_set_name(device, "Lenovo hid device receiver 6201");
	} else if (pid == 0x629d) {
		fu_device_set_name(device, "Lenovo hid device receiver 629d");
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "PID 0x%04X is not a supported Lenovo HID receiver",
			    pid);
		return FALSE;
	}
	fu_device_add_protocol(device, "com.lenovo.accessory");
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_device_probe(FuDevice *device, GError **error)
{
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLenovoAccessoryHidDevice *self = FU_LENOVO_HID_DEVICE(device);
	g_autoptr(GByteArray) req = g_byte_array_new();

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 100, "switch-dfu");
	fu_lenovo_accessory_hid_dfu_set_devicemode(FU_HIDRAW_DEVICE(self), 2, error);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_progress_step_done(progress);
	return TRUE;
}

static void
fu_lenovo_accessory_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 26, "reload");
}

static void
fu_lenovo_accessory_hid_device_class_init(FuLenovoAccessoryHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_lenovo_accessory_hid_device_setup;
	device_class->set_progress = fu_lenovo_accessory_hid_device_set_progress;
	device_class->detach = fu_lenovo_accessory_hid_device_detach;
	device_class->probe = fu_lenovo_accessory_hid_device_probe;
}

static void
fu_lenovo_accessory_hid_device_init(FuLenovoAccessoryHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	/*fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);*/
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_RECEIVER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}
