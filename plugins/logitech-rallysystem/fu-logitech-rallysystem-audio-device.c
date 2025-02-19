/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-rallysystem-audio-device.h"
#include "fu-logitech-rallysystem-struct.h"

#define TOPOLOGY_DATA_LEN		513 /* plus 1 byte for the report id */
#define SERIAL_NUMBER_REQUEST_DATA_LEN	49
#define SERIAL_NUMBER_RESPONSE_DATA_LEN 128

struct _FuLogitechRallysystemAudioDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechRallysystemAudioDevice,
	      fu_logitech_rallysystem_audio_device,
	      FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_logitech_rallysystem_audio_device_set_version(FuLogitechRallysystemAudioDevice *self,
						 GError **error)
{
	guint8 buf[TOPOLOGY_DATA_LEN] = {0x3E, 0x0};
	guint32 fwversion = 0;

	/* setup HID report to query current device version */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  sizeof(buf),
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		return FALSE;
	}
	if (!fu_memread_uint24_safe(
		buf,
		sizeof(buf),
		0xB8, /* topology size of 12 bytes * 15 elements, plus an offset */
		&fwversion,
		G_BIG_ENDIAN,
		error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), fwversion);

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
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf_req,
					  sizeof(buf_req),
					  FU_IOCTL_FLAG_RETRY,
					  error))
		return FALSE;

	/* wait 100ms for device to consume request and prepare for response */
	fu_device_sleep(FU_DEVICE(self), 100);

	/* setup HID report to query serial number */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf_res,
					  sizeof(buf_res),
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		return FALSE;
	}

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

static void
fu_logitech_rallysystem_audio_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 100, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_logitech_rallysystem_audio_device_convert_version(FuDevice *device, guint64 version_raw)
{
	guint8 major = 0;
	guint8 minor = 0;
	guint8 build = 0;
	/*
	 * device reports system version in 3 bytes: major.minor.build
	 * convert major.minor.build -> major.minor.0.build
	 */
	major = (version_raw >> 16) & 0xFF;
	minor = (version_raw >> 8) & 0xFF;
	build = (version_raw >> 0) & 0xFF;
	version_raw = (((guint32)major) << 24) | (((guint32)minor) << 16) | (((guint32)build) << 0);
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_logitech_rallysystem_audio_device_init(FuLogitechRallysystemAudioDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.rallysystem");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_logitech_rallysystem_audio_device_class_init(FuLogitechRallysystemAudioDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_logitech_rallysystem_audio_device_setup;
	device_class->set_progress = fu_logitech_rallysystem_audio_device_set_progress;
	device_class->convert_version = fu_logitech_rallysystem_audio_device_convert_version;
}
