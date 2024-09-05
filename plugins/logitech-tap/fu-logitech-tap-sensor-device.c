/*
 * Copyright 1999-2023 Logitech, Inc.
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

#define FU_LOGITECH_TAP_SENSOR_DEVICE_IOCTL_TIMEOUT 50000 /* ms */
#define HID_SET_DATA_LEN			    5
#define HID_GET_DATA_LEN			    5

#ifndef HIDIOCGINPUT
#define HIDIOCGINPUT(len) _IOC(_IOC_READ, 'H', 0x0A, len)
#endif

/* device version */
const guint8 kHidReportIdAppSetCmd = 0x1b;
const guint8 kHidReportIdAppGetCmd = 0x19;
const guint8 kColossusAppCmdGetVer = 0x04;

/* enable/disable TDE mode */
const guint8 kHidMcuTdeReportId = 0x1A;
const guint8 kHidMcuTdeModeSelector = 0x02;
const guint8 kHidMcuTdeModeEnable = 0x01;
const guint8 kHidMcuTdeModeDisable = 0x00;

/* serial number of the device */
const guint8 kHidMcuCmdSetSerialNumber = 0x1C;
const guint8 kHidMcuCmdGetSerialNumber = 0x1D;
const guint8 kHidMcuSerialNumberSetReportByte1 = 0x00;
const guint8 kHidMcuSerialNumberSetReportByte2 = 0x70;
const guint8 kHidMcuSerialNumberSetReportByte3 = 0x0E;
const guint8 kHidMcuSerialNumberSetReportByte4 = 0x00;

/* reboot device */
const guint8 kHidReportIdMcuSetCmd = 0x1a;

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
	guint8 buf[HID_SET_DATA_LEN] = {
	    kHidMcuTdeReportId,
	    kHidMcuTdeModeSelector,
	    kHidMcuTdeModeEnable,
	    0,
	    0,
	};
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    buf,
					    sizeof(buf),
					    FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					    error);
}

static gboolean
fu_logitech_tap_sensor_device_disable_tde(FuDevice *device, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	guint8 buf[HID_SET_DATA_LEN] = {
	    kHidMcuTdeReportId,
	    kHidMcuTdeModeSelector,
	    kHidMcuTdeModeDisable,
	    0,
	    0,
	};
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    buf,
					    sizeof(buf),
					    FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					    error);
}

gboolean
fu_logitech_tap_sensor_device_reboot_device(FuLogitechTapSensorDevice *self, GError **error)
{
	const guint8 pinclr = 05;
	const guint8 pinset = 06;
	const guint8 PWR = 45;
	const guint8 RST = 46;
	guint8 set_data[HID_SET_DATA_LEN] = {kHidReportIdMcuSetCmd, pinclr, PWR, 0, 0};
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

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
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  set_data,
					  sizeof(set_data),
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	set_data[1] = pinclr;
	set_data[2] = RST;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  set_data,
					  sizeof(set_data),
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000); /* 2 sec */
	set_data[1] = pinset;
	set_data[2] = PWR;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  set_data,
					  sizeof(set_data),
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000); /* 2 sec */
	set_data[1] = pinset;
	set_data[2] = RST;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  set_data,
					  sizeof(set_data),
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
	guint8 set_data[HID_SET_DATA_LEN] = {kHidReportIdAppSetCmd, kColossusAppCmdGetVer, 0, 0, 0};
	guint8 get_data[HID_GET_DATA_LEN] = {kHidReportIdAppGetCmd, 0, 0, 0, 0};

	/* setup HID report to query current device version */
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  set_data,
					  sizeof(set_data),
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;
	if (!fu_logitech_tap_sensor_device_get_feature(self,
						       (guint8 *)get_data,
						       HID_GET_DATA_LEN,
						       error))
		return FALSE;

	/* MinorVersion byte 3, MajorVersion byte 4, BuildVersion byte 2 & 1 */
	if (!fu_memread_uint32_safe(get_data,
				    sizeof(get_data),
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
	guint8 set_data[HID_SET_DATA_LEN] = {kHidMcuCmdSetSerialNumber,
					     kHidMcuSerialNumberSetReportByte1,
					     kHidMcuSerialNumberSetReportByte2,
					     kHidMcuSerialNumberSetReportByte3,
					     kHidMcuSerialNumberSetReportByte4};

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
					  set_data,
					  sizeof(set_data),
					  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), kLogiDefaultSensorSleepIntervalMs); /* 50 ms */
	/* serial number is a 12-byte-string that is stored in MCU  */
	/* each get request fetches 1 word (4 bytes), so iterate 3 times */
	for (int index = 1; index <= 3; index++) {
		guint8 get_data[HID_GET_DATA_LEN] = {kHidMcuCmdGetSerialNumber, 0, 0, 0, 0};
		if (!fu_logitech_tap_sensor_device_get_feature(self,
							       (guint8 *)get_data,
							       HID_GET_DATA_LEN,
							       error))
			return FALSE;
		g_string_append_printf(serial_number,
				       "%c%c%c%c",
				       get_data[1],
				       get_data[2],
				       get_data[3],
				       get_data[4]);
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
