/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-firmware.h"
#include "fu-goodixtp-hid-device.h"

typedef struct {
	gchar *patch_pid;
	gchar *patch_vid;
	guint8 sensor_id;
	guint8 cfg_ver;
} FuGoodixtpHidDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuGoodixtpHidDevice, fu_goodixtp_hid_device, FU_TYPE_UDEV_DEVICE)
#define GET_PRIVATE(o) (fu_goodixtp_hid_device_get_instance_private(o))

#define GOODIX_DEVICE_IOCTL_TIMEOUT 5000

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
	fu_string_append(str, idt, "patch_pid", priv->patch_pid);
	fu_string_append(str, idt, "patch_vid", priv->patch_vid);
	fu_string_append_kx(str, idt, "sensor_id", priv->sensor_id);
	fu_string_append_kx(str, idt, "cfg_ver", priv->cfg_ver);
}

gboolean
fu_goodixtp_hid_device_get_report(FuGoodixtpHidDevice *self,
				  guint8 *buf,
				  guint32 bufsz,
				  GError **error)
{
#ifdef HAVE_HIDRAW_H
	guint8 rcv_buf[PACKAGE_LEN + 1] = {0};

	rcv_buf[0] = REPORT_ID;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(PACKAGE_LEN),
				  rcv_buf,
				  NULL,
				  GOODIX_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed get report: ");
		return FALSE;
	}
	if (rcv_buf[0] != REPORT_ID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "rcv_buf[0]:%02x != 0x0E",
			    rcv_buf[0]);
		return FALSE;
	}

	if (!fu_memcpy_safe(buf, bufsz, 0, rcv_buf, sizeof(rcv_buf), 0, PACKAGE_LEN, error))
		return FALSE;
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

gboolean
fu_goodixtp_hid_device_set_report(FuGoodixtpHidDevice *self,
				  guint8 *buf,
				  guint32 len,
				  GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCSFEATURE(len),
				  buf,
				  NULL,
				  GOODIX_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed set report: ");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_goodixtp_hid_device_probe(FuDevice *device, GError **error)
{
	/* check is valid */
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
fu_goodixtp_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_goodixtp_hid_device_init(FuGoodixtpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "com.goodix.goodixtp");
	fu_device_set_name(FU_DEVICE(self), "Touch Controller Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Goodix inc.");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK);
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_goodixtp_hid_device_finalize;
	klass_device->to_string = fu_goodixtp_hid_device_to_string;
	klass_device->probe = fu_goodixtp_hid_device_probe;
	klass_device->set_progress = fu_goodixtp_hid_device_set_progress;
}
