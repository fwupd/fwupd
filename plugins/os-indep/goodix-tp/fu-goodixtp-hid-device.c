/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-firmware.h"
#include "fu-goodixtp-hid-device.h"

typedef struct {
	gchar *patch_pid;
	gchar *patch_vid;
	guint8 sensor_id;
	guint8 cfg_ver;
} FuGoodixtpHidDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuGoodixtpHidDevice, fu_goodixtp_hid_device, FU_TYPE_HIDRAW_DEVICE)
#define GET_PRIVATE(o) (fu_goodixtp_hid_device_get_instance_private(o))

void
fu_goodixtp_hid_device_set_patch_pid(FuGoodixtpHidDevice *self, const gchar *patch_pid)
{
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	priv->patch_pid = g_strdup_printf("GT%s", patch_pid);
}

void
fu_goodixtp_hid_device_set_patch_vid(FuGoodixtpHidDevice *self, const gchar *patch_vid)
{
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	priv->patch_vid = g_strdup(patch_vid);
}

void
fu_goodixtp_hid_device_set_sensor_id(FuGoodixtpHidDevice *self, guint8 sensor_id)
{
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	priv->sensor_id = sensor_id;
}

void
fu_goodixtp_hid_device_set_config_ver(FuGoodixtpHidDevice *self, guint8 ver)
{
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	priv->cfg_ver = ver;
}

guint8
fu_goodixtp_hid_device_get_sensor_id(FuGoodixtpHidDevice *self)
{
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	return priv->sensor_id;
}

static void
fu_goodixtp_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "patch_pid", priv->patch_pid);
	fwupd_codec_string_append(str, idt, "patch_vid", priv->patch_vid);
	fwupd_codec_string_append_hex(str, idt, "sensor_id", priv->sensor_id);
	fwupd_codec_string_append_hex(str, idt, "cfg_ver", priv->cfg_ver);
}

gboolean
fu_goodixtp_hid_device_get_report(FuGoodixtpHidDevice *self,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	guint8 rcv_buf[PACKAGE_LEN + 1] = {0};

	rcv_buf[0] = REPORT_ID;
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  rcv_buf,
					  sizeof(rcv_buf),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error, "failed get report: ");
		return FALSE;
	}
	if (rcv_buf[0] != REPORT_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "rcv_buf[0]:%02x != 0x0E",
			    rcv_buf[0]);
		return FALSE;
	}

	if (!fu_memcpy_safe(buf, bufsz, 0, rcv_buf, sizeof(rcv_buf), 0, PACKAGE_LEN, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_goodixtp_hid_device_set_report(FuGoodixtpHidDevice *self,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  bufsz,
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error, "failed set report: ");
		return FALSE;
	}
	return TRUE;
}

static void
fu_goodixtp_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_goodixtp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_goodixtp_hid_device_init(FuGoodixtpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.goodix.goodixtp");
	fu_device_set_name(FU_DEVICE(self), "Touch Controller Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Goodix inc.");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_goodixtp_hid_device_finalize(GObject *object)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(object);
	FuGoodixtpHidDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->patch_pid);
	g_free(priv->patch_vid);
	G_OBJECT_CLASS(fu_goodixtp_hid_device_parent_class)->finalize(object);
}

static void
fu_goodixtp_hid_device_class_init(FuGoodixtpHidDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_goodixtp_hid_device_finalize;
	device_class->to_string = fu_goodixtp_hid_device_to_string;
	device_class->set_progress = fu_goodixtp_hid_device_set_progress;
	device_class->convert_version = fu_goodixtp_hid_device_convert_version;
}
