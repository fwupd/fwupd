/*
 * Copyright 2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/types.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#ifdef HAVE_IOCTL_H
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#endif

#include <string.h>

#include "fu-logitech-tap-sensor-device.h"
#include "fu-logitech-tap-struct.h"

#define FU_LOGITECH_TAP_SENSOR_DEVICE_IOCTL_TIMEOUT 50000 /* ms */

#ifndef HIDIOCGINPUT
#define HIDIOCGINPUT(len) _IOC(_IOC_READ, 'H', 0x0A, len)
#endif

const guint kLogiDefaultSensorSleepIntervalMs = 50;

struct _FuLogitechTapSensorDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapSensorDevice, fu_logitech_tap_sensor_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_logitech_tap_sensor_device_get_feature(FuLogitechTapSensorDevice *self,
					  guint8 *data,
					  guint datasz,
					  GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* try HIDIOCGINPUT request in case of failure */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  data,
					  datasz,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  &error_local)) {
		g_debug("failed to send get request, retrying: %s", error_local->message);
		if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
					  HIDIOCGINPUT(datasz),
					  data,
					  datasz,
					  NULL,
					  FU_LOGITECH_TAP_SENSOR_DEVICE_IOCTL_TIMEOUT,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_enable_tde(FuDevice *device, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	g_autoptr(FuStructLogitechTapSensorHidReq) st = fu_struct_logitech_tap_sensor_hid_req_new();

	fu_struct_logitech_tap_sensor_hid_req_set_cmd(st, FU_LOGITECH_TAP_SENSOR_HID_SET_CMD_TDE);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte1(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_TDE_MODE_SELECTOR);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_TDE_MODE_ENABLE);

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st->data,
					    st->len,
					    FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					    error);
}

static gboolean
fu_logitech_tap_sensor_device_disable_tde(FuDevice *device, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	g_autoptr(FuStructLogitechTapSensorHidReq) st = fu_struct_logitech_tap_sensor_hid_req_new();

	fu_struct_logitech_tap_sensor_hid_req_set_cmd(st, FU_LOGITECH_TAP_SENSOR_HID_SET_CMD_TDE);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte1(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_TDE_MODE_SELECTOR);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_TDE_MODE_DISABLE);

	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    st->data,
					    st->len,
					    FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					    error);
}

gboolean
fu_logitech_tap_sensor_device_reboot_device(FuLogitechTapSensorDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuStructLogitechTapSensorHidReq) st = fu_struct_logitech_tap_sensor_hid_req_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 100, "attach");
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);

	/* need to reopen the device, as at composite_cleanup time, device is already closed */
	if (!fu_device_open(FU_DEVICE(self), error))
		return FALSE;

	/* enable/disable TDE mode */
	locker =
	    fu_device_locker_new_full(FU_DEVICE(self),
				      (FuDeviceLockerFunc)fu_logitech_tap_sensor_device_enable_tde,
				      (FuDeviceLockerFunc)fu_logitech_tap_sensor_device_disable_tde,
				      error);
	if (locker == NULL)
		return FALSE;

	/* setup HID report for power cycle */
	fu_struct_logitech_tap_sensor_hid_req_set_cmd(st,
						      FU_LOGITECH_TAP_SENSOR_HID_SET_CMD_REBOOT);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte1(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_REBOOT_PIN_CLR);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_REBOOT_PWR);
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st->data,
					  st->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_REBOOT_RST);
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st->data,
					  st->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000); /* 2 sec */

	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte1(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_REBOOT_PIN_SET);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_REBOOT_PWR);
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st->data,
					  st->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000); /* 2 sec */

	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st,
	    FU_LOGITECH_TAP_SENSOR_HID_REBOOT_RST);
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st->data,
					  st->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_set_version(FuLogitechTapSensorDevice *self, GError **error)
{
	guint32 version = 0;
	g_autoptr(FuStructLogitechTapSensorHidReq) st_req =
	    fu_struct_logitech_tap_sensor_hid_req_new();
	g_autoptr(FuStructLogitechTapSensorHidRes) st_res =
	    fu_struct_logitech_tap_sensor_hid_res_new();

	fu_struct_logitech_tap_sensor_hid_req_set_cmd(st_req,
						      FU_LOGITECH_TAP_SENSOR_HID_SET_CMD_VERSION);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte1(
	    st_req,
	    FU_LOGITECH_TAP_SENSOR_HID_COLOSSUS_APP_GET_VERSION);
	fu_struct_logitech_tap_sensor_hid_res_set_cmd(st_res,
						      FU_LOGITECH_TAP_SENSOR_HID_GET_CMD_VERSION);
	/* setup HID report to query current device version */
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_req->data,
					  st_req->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;
	if (!fu_logitech_tap_sensor_device_get_feature(self,
						       (guint8 *)st_res->data,
						       st_res->len,
						       error))
		return FALSE;

	/* MinorVersion byte 3, MajorVersion byte 4, BuildVersion byte 2 & 1 */
	if (!fu_memread_uint32_safe((guint8 *)st_res->data,
				    st_res->len,
				    0x01,
				    &version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_set_serial(FuLogitechTapSensorDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GString) serial_number = g_string_new(NULL);
	g_autoptr(FuStructLogitechTapSensorHidReq) st_req =
	    fu_struct_logitech_tap_sensor_hid_req_new();

	fu_struct_logitech_tap_sensor_hid_req_set_cmd(
	    st_req,
	    FU_LOGITECH_TAP_SENSOR_HID_SET_CMD_SERIAL_NUMBER);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte1(
	    st_req,
	    FU_LOGITECH_TAP_SENSOR_HID_SERIAL_NUMBER_SET_REPORT_BYTE1);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte2(
	    st_req,
	    FU_LOGITECH_TAP_SENSOR_HID_SERIAL_NUMBER_SET_REPORT_BYTE2);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte3(
	    st_req,
	    FU_LOGITECH_TAP_SENSOR_HID_SERIAL_NUMBER_SET_REPORT_BYTE3);
	fu_struct_logitech_tap_sensor_hid_req_set_payload_byte4(
	    st_req,
	    FU_LOGITECH_TAP_SENSOR_HID_SERIAL_NUMBER_SET_REPORT_BYTE4);

	/* enable/disable TDE mode */
	locker =
	    fu_device_locker_new_full(FU_DEVICE(self),
				      (FuDeviceLockerFunc)fu_logitech_tap_sensor_device_enable_tde,
				      (FuDeviceLockerFunc)fu_logitech_tap_sensor_device_disable_tde,
				      error);
	if (locker == NULL)
		return FALSE;

	/* setup HID report for serial number */
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_req->data,
					  st_req->len,
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), kLogiDefaultSensorSleepIntervalMs); /* 50 ms */
	/* serial number is a 12-byte-string that is stored in MCU  */
	/* each get request fetches 1 word (4 bytes), so iterate 3 times */
	for (int index = 1; index <= 3; index++) {
		g_autoptr(FuStructLogitechTapSensorHidRes) st_res =
		    fu_struct_logitech_tap_sensor_hid_res_new();

		fu_struct_logitech_tap_sensor_hid_res_set_cmd(
		    st_res,
		    FU_LOGITECH_TAP_SENSOR_HID_GET_CMD_SERIAL_NUMBER);

		if (!fu_logitech_tap_sensor_device_get_feature(self,
							       (guint8 *)st_res->data,
							       st_res->len,
							       error))
			return FALSE;
		g_string_append_printf(serial_number,
				       "%c%c%c%c",
				       st_res->data[1],
				       st_res->data[2],
				       st_res->data[3],
				       st_res->data[4]);
	}
	fu_device_set_serial(FU_DEVICE(self), serial_number->str);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_setup(FuDevice *device, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);

	if (!fu_logitech_tap_sensor_device_set_version(self, error))
		return FALSE;
	if (!fu_logitech_tap_sensor_device_set_serial(self, error))
		return FALSE;
	return TRUE;
}

static void
fu_logitech_tap_sensor_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 100, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_logitech_tap_sensor_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_logitech_tap_sensor_device_init(FuLogitechTapSensorDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.hardware.tap");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_logitech_tap_sensor_device_class_init(FuLogitechTapSensorDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_logitech_tap_sensor_device_setup;
	device_class->set_progress = fu_logitech_tap_sensor_device_set_progress;
	device_class->convert_version = fu_logitech_tap_sensor_device_convert_version;
}
