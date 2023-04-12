/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

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
#define HID_SET_DATA_LEN 5
#define HID_GET_DATA_LEN 5

#define HIDIOCGINPUT(len)    _IOC(_IOC_READ, 'H', 0x0A, len)

/* device version */
const guchar kHidReportIdAppSetCmd = 0x1b;
const guchar kHidReportIdAppGetCmd = 0x19;
const guchar kColossusAppCmdGetVer = 0x04;

/* enable/disable TDE mode */
const guchar kHidMcuTdeReportId = 0x1A;
const guchar kHidMcuTdeModeSelector = 0x02;
const guchar kHidMcuTdeModeEnable = 0x01;
const guchar kHidMcuTdeModeDisable = 0x00;

/* serial number of the device */
const guchar kHidMcuCmdSetSerialNumber = 0x1C;
const guchar kHidMcuCmdGetSerialNumber = 0x1D;
const guchar kHidMcuSerialNumberSetReportByte1 = 0x00;
const guchar kHidMcuSerialNumberSetReportByte2 = 0x70;
const guchar kHidMcuSerialNumberSetReportByte3 = 0x0E;
const guchar kHidMcuSerialNumberSetReportByte4 = 0x00;

/* reboot device */
const guchar kHidReportIdMcuSetCmd = 0x1a;

const guint kLogiDefaultSensorSleepIntervalMs = 50;

struct _FuLogitechTapSensorDevice {
	FuLogitechTapDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapSensorDevice, fu_logitech_tap_sensor_device, FU_TYPE_LOGITECH_TAP_DEVICE)

static gboolean
fu_logitech_tap_sensor_device_set_feature(FuLogitechTapSensorDevice *self, const guint8 *data, guint datasz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "HidSetFeature", data, datasz);
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				    HIDIOCSFEATURE(datasz),
				    (guint8 *)data,
				    NULL,
				    FU_LOGITECH_TAP_SENSOR_DEVICE_IOCTL_TIMEOUT,
				    error);
#else
    /* failed */
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_logitech_tap_sensor_device_get_feature(FuLogitechTapSensorDevice *self, guint8 *data, guint datasz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	g_autoptr(GError) error_local = NULL;
	fu_dump_raw(G_LOG_DOMAIN, "HidGetFeatureReq", data, datasz);

	/* try HIDIOCGINPUT request in case of failure */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(datasz),
				  data,
				  NULL,
				  FU_LOGITECH_TAP_SENSOR_DEVICE_IOCTL_TIMEOUT,
				  &error_local)) {
		g_debug("failed to send get request, retrying: %s", error_local->message);
		if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGINPUT(datasz),
				  data,
				  NULL,
				  FU_LOGITECH_TAP_SENSOR_DEVICE_IOCTL_TIMEOUT,
				  error))
			return FALSE;
	}

	fu_dump_raw(G_LOG_DOMAIN, "HidGetFeatureRes", data, datasz);

	return TRUE;
#else
	/* failed */
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_logitech_tap_sensor_device_set_tde(FuDevice *device, const guchar tde_mode, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	guint8 set_data[HID_SET_DATA_LEN] = {kHidMcuTdeReportId, kHidMcuTdeModeSelector, tde_mode, 0, 0};

	/* enable/disable TDE mode */
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error))
	return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_reboot_device(FuDevice *device,
				   FuProgress *progress,
				   GError **error)
{
    FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	gint8 pinclr = 05;
 	guint8 pinset = 06;
 	guint8 PWR = 45;
 	guint8 RST = 46;
 	guint8 set_data[HID_SET_DATA_LEN] = {kHidReportIdMcuSetCmd, pinclr, PWR, 0, 0};

	g_debug("trigger device reboot");
	/* enable TDE mode */
	if (!fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeEnable, error))
		return FALSE;

	/* setup HID report for power cycle */
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error)) {
		/* disable TDE mode */
		fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error);
		return FALSE;
	}
	set_data[1] = pinclr;
    set_data[2] = RST;
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error)) {
		/* disable TDE mode */
		fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error);
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 4000); /* 2 sec */
    set_data[1] = pinset;
    set_data[2] = PWR;
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error)) {
		/* disable TDE mode */
		fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error);
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 4000); /* 2 sec */
    set_data[1] = pinset;
    set_data[2] = RST;
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error)) {

		/* disable TDE mode */
		fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error);
		return FALSE;
	}
	/* disable TDE mode */
	if (!fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error))
		return FALSE;

    /* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_set_version(FuDevice *device, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	guint32 fwversion = 0;
	guint8 set_data[HID_SET_DATA_LEN] = {kHidReportIdAppSetCmd, kColossusAppCmdGetVer, 0, 0, 0};
	guint8 get_data[HID_GET_DATA_LEN] = {kHidReportIdAppGetCmd, 0, 0, 0, 0};

	g_debug("get sensor firmware version");

	/* setup HID report to query current device version */
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error))
	return FALSE;
	if (!fu_logitech_tap_sensor_device_get_feature(self,
						      (guchar *)get_data,
							  HID_GET_DATA_LEN,
						      error))
	return FALSE;

	/* little-endian data. MinorVersion byte 3, MajorVersion byte 4, BuildVersion byte 2 & 1 */
	fwversion =
	    (get_data[4] << 24) + (get_data[3] << 16) + (get_data[2] << 8) + get_data[1];
	fu_device_set_version_from_uint32(device, fwversion);
	
	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_set_serial(FuDevice *device, GError **error)
{
	FuLogitechTapSensorDevice *self = FU_LOGITECH_TAP_SENSOR_DEVICE(device);
	g_autoptr(GString) serial_number = g_string_new(NULL);
	guint8 set_data[HID_SET_DATA_LEN] = {kHidMcuCmdSetSerialNumber, kHidMcuSerialNumberSetReportByte1, 
	kHidMcuSerialNumberSetReportByte2, kHidMcuSerialNumberSetReportByte3, kHidMcuSerialNumberSetReportByte4};

	g_debug("get sensor serial number");

	/* setup HID report for serial number */
	if (!fu_logitech_tap_sensor_device_set_feature(self,
						      (guchar *)set_data,
							  HID_SET_DATA_LEN,
						      error))
	return FALSE;
	fu_device_sleep(FU_DEVICE(self), kLogiDefaultSensorSleepIntervalMs); /* 50 ms */
 	/* serial number is a 12-byte-string that is stored in MCU  */
 	/* each get request fetchs 1 word (4 bytes), so iterate 3 times */
    for (int index = 1; index <= 3; index++) {
	    guint8 get_data[HID_GET_DATA_LEN] = {kHidMcuCmdGetSerialNumber, 0, 0, 0, 0};
	    if (!fu_logitech_tap_sensor_device_get_feature(self,
						      (guchar *)get_data,
							  HID_GET_DATA_LEN,
						      error))
	        return FALSE;
	    g_string_append_printf(serial_number, "%c%c%c%c", (guchar)get_data[1], 
	       (guchar)get_data[2], (guchar)get_data[3], (guchar)get_data[4]);
	}
		
    fu_device_set_serial(device, serial_number->str);
	
	/* success */
	return TRUE;
}


static gboolean
fu_logitech_tap_sensor_device_setup(FuDevice *device, GError **error)
{
	if (!fu_logitech_tap_sensor_device_set_version(device, error))
		return FALSE;
	/* enable TDE mode */
	if (!fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeEnable, error))
		return FALSE;
	/* get serial number */
	if (!fu_logitech_tap_sensor_device_set_serial(device, error)) {
		/* disable TDE mode */
		fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error);
		return FALSE;
	}
	/* disable TDE mode */
	if (!fu_logitech_tap_sensor_device_set_tde(device, kHidMcuTdeModeDisable, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_logitech_tap_sensor_device_probe(FuDevice *device, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 3)
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_logitech_tap_sensor_device_parent_class)->probe(device, error)) {
		return FALSE;
	}

	/* ignore unsupported susbystems */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* set the physical ID */ 
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
#else
    /* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "this version of GUsb is not supported");
	return FALSE;
#endif
}

static void
fu_logitech_tap_sensor_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_logitech_tap_sensor_device_init(FuLogitechTapSensorDevice *self)
{
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK | FU_UDEV_DEVICE_FLAG_IOCTL_RETRY);
}

static void
fu_logitech_tap_sensor_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_logitech_tap_sensor_device_parent_class)->finalize(object);
}

static void
fu_logitech_tap_sensor_device_class_init(FuLogitechTapSensorDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuLogitechTapDeviceClass *klass_logitech_tap_device = FU_LOGITECH_TAP_DEVICE_CLASS(klass);
    object_class->finalize = fu_logitech_tap_sensor_device_finalize;
	klass_device->probe = fu_logitech_tap_sensor_device_probe;
	klass_device->setup = fu_logitech_tap_sensor_device_setup;
	klass_device->set_progress = fu_logitech_tap_sensor_device_set_progress;
	klass_logitech_tap_device->reboot_device = fu_logitech_tap_sensor_device_reboot_device;
}
