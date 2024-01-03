/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_IOCTL_H
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#endif

#include "fu-logitech-rallysystem-audio-device.h"
#include "fu-logitech-rallysystem-struct.h"

#define FU_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE_IOCTL_TIMEOUT 2500 /* ms */
#define TOPOLOGY_DATA_LEN				   513	/* plus 1 byte for the report id */
#define SERIAL_NUMBER_REQUEST_DATA_LEN			   49
#define SERIAL_NUMBER_RESPONSE_DATA_LEN			   128

struct _FuLogitechRallysystemAudioDevice {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechRallysystemAudioDevice,
	      fu_logitech_rallysystem_audio_device,
	      FU_TYPE_UDEV_DEVICE)

static gboolean
fu_logitech_rallysystem_audio_device_set_feature(FuLogitechRallysystemAudioDevice *self,
						 const guint8 *buf,
						 guint bufsz,
						 GError **error)
{
#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "HidSetFeature", buf, bufsz);
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				    HIDIOCSFEATURE(bufsz),
				    (guint8 *)buf,
				    NULL,
				    FU_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE_IOCTL_TIMEOUT,
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
fu_logitech_rallysystem_audio_device_get_feature(FuLogitechRallysystemAudioDevice *self,
						 guint8 *buf,
						 guint bufsz,
						 GError **error)
{
#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "HidGetFeatureReq", buf, bufsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(bufsz),
				  buf,
				  NULL,
				  FU_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "HidGetFeatureRes", buf, bufsz);
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
fu_logitech_rallysystem_audio_device_set_version(FuLogitechRallysystemAudioDevice *self,
						 GError **error)
{
	guint8 buf[TOPOLOGY_DATA_LEN] = {0x3E, 0x0};
	guint32 fwversion = 0;

	/* setup HID report to query current device version */
	if (!fu_logitech_rallysystem_audio_device_get_feature(self, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_memread_uint24_safe(
		buf,
		sizeof(buf),
		0xB8, /* topology size of 12 bytes * 15 elements, plus an offset */
		&fwversion,
		G_BIG_ENDIAN,
		error))
		return FALSE;
	fu_device_set_version_u32(FU_DEVICE(self), fwversion);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_audio_device_set_serial(FuLogitechRallysystemAudioDevice *self,
						GError **error)
{
	guint8 buf_req[SERIAL_NUMBER_REQUEST_DATA_LEN] =
	    {0x28, 0x85, 0x08, 0xBB, 0x1B, 0x00, 0x01, 0x30, 0, 0, 0, 0x0C};
	guint8 buf_res[SERIAL_NUMBER_RESPONSE_DATA_LEN] = {0x29, 0x0};
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GString) serial = g_string_new(NULL);

	/* setup HID report for serial number request */
	if (!fu_logitech_rallysystem_audio_device_set_feature(self,
							      buf_req,
							      sizeof(buf_req),
							      error))
		return FALSE;

	/* wait 100ms for device to consume request and prepare for response */
	fu_device_sleep(FU_DEVICE(self), 100);

	/* setup HID report to query serial number */
	if (!fu_logitech_rallysystem_audio_device_get_feature(self,
							      buf_res,
							      sizeof(buf_res),
							      error))
		return FALSE;

	/* desired serial number format: PID:YYYYMMDD:EthernetMacAddress */
	st = fu_struct_audio_serial_number_parse(buf_res, sizeof(buf_res), 0x0, error);
	if (st == NULL)
		return FALSE;
	g_string_append_printf(serial,
			       "%04x:%04u%02u%02u:",
			       fu_struct_audio_serial_number_get_pid(st),
			       fu_struct_audio_serial_number_get_year(st),
			       fu_struct_audio_serial_number_get_month(st),
			       fu_struct_audio_serial_number_get_day(st));
	for (guint i = 0x0; i < FU_STRUCT_AUDIO_SERIAL_NUMBER_SIZE_MAC_ADDRESS; i++)
		g_string_append_printf(serial, "%02x", st->data[i]);
	fu_device_set_serial(FU_DEVICE(self), serial->str);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_audio_device_setup(FuDevice *device, GError **error)
{
	FuLogitechRallysystemAudioDevice *self = FU_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE(device);
	if (!fu_logitech_rallysystem_audio_device_set_version(self, error))
		return FALSE;
	if (!fu_logitech_rallysystem_audio_device_set_serial(self, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_audio_device_probe(FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_logitech_rallysystem_audio_device_parent_class)
		 ->probe(device, error))
		return FALSE;

	/* ignore unsupported subsystems */
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
}

static void
fu_logitech_rallysystem_audio_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 100, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_logitech_rallysystem_audio_device_init(FuLogitechRallysystemAudioDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.rallysystem");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_READ);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_IOCTL_RETRY);
}

static void
fu_logitech_rallysystem_audio_device_class_init(FuLogitechRallysystemAudioDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_logitech_rallysystem_audio_device_probe;
	klass_device->setup = fu_logitech_rallysystem_audio_device_setup;
	klass_device->set_progress = fu_logitech_rallysystem_audio_device_set_progress;
}
