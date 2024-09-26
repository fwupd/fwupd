/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuV4lDevice"

#include "config.h"

#ifdef HAVE_VIDEODEV2_H
#include <linux/videodev2.h>
#endif

#include "fu-string.h"
#include "fu-usb-device.h"
#include "fu-v4l-device.h"

/**
 * FuV4lDevice
 *
 * See also: #FuUdevDevice
 */

typedef struct {
	guint8 index;
	FuV4lCap caps;
} FuV4lDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuV4lDevice, fu_v4l_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_v4l_device_get_instance_private(o))

static void
fu_v4l_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuV4lDevice *self = FU_V4L_DEVICE(device);
	FuV4lDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "Index", priv->index);
	if (priv->caps != FU_V4L_CAP_NONE) {
		g_autofree gchar *caps = fu_v4l_cap_to_string(priv->caps);
		fwupd_codec_string_append(str, idt, "Caps", caps);
	}
}

/**
 * fu_v4l_device_get_index:
 * @self: a #FuV4lDevice
 *
 * Gets the video4linux device index.
 *
 * Returns: integer, or %G_MAXUINT8 on error
 *
 * Since: 2.0.0
 **/
guint8
fu_v4l_device_get_index(FuV4lDevice *self)
{
	FuV4lDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_V4L_DEVICE(self), G_MAXUINT8);
	return priv->index;
}

/**
 * fu_v4l_device_get_caps:
 * @self: a #FuV4lDevice
 *
 * Gets the video4linux device capabilities.
 *
 * NOTE: This property is only available after the device has been opened and is not available
 * during probe.
 *
 * Returns: integer, or 0 on error
 *
 * Since: 2.0.0
 **/
FuV4lCap
fu_v4l_device_get_caps(FuV4lDevice *self)
{
	FuV4lDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_V4L_DEVICE(self), 0);
	return priv->caps;
}

static gboolean
fu_v4l_device_usb_probe(FuV4lDevice *self, FuDevice *usb_device, GError **error)
{
	/* copy the VID and PID, and reconstruct compatible IDs */
	if (!fu_device_probe(usb_device, error))
		return FALSE;
	fu_device_add_instance_str(FU_DEVICE(self),
				   "VID",
				   fu_device_get_instance_str(usb_device, "VID"));
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "USB",
					      "VID",
					      NULL))
		return FALSE;
	fu_device_add_instance_str(FU_DEVICE(self),
				   "VEN",
				   fu_device_get_instance_str(usb_device, "VID"));
	fu_device_add_instance_str(FU_DEVICE(self),
				   "DEV",
				   fu_device_get_instance_str(usb_device, "PID"));
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "VIDEO4LINUX", "VEN", "DEV", NULL))
		return FALSE;
	fu_device_incorporate(FU_DEVICE(self),
			      usb_device,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

	/* success */
	return TRUE;
}

static gboolean
fu_v4l_device_probe(FuDevice *device, GError **error)
{
	FuV4lDevice *self = FU_V4L_DEVICE(device);
	g_autofree gchar *attr_index = NULL;
	g_autofree gchar *attr_name = NULL;
	g_autoptr(FuDevice) usb_device = NULL;

	/* name */
	attr_name = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					      "name",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
	if (attr_name != NULL)
		fu_device_set_name(device, attr_name);

	/* device index */
	attr_index = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					       "index",
					       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					       NULL);
	if (attr_index != NULL) {
		guint64 index64 = 0;
		if (!fu_strtoull(attr_index,
				 &index64,
				 0,
				 G_MAXUINT8,
				 FU_INTEGER_BASE_AUTO,
				 error)) {
			g_prefix_error(error, "failed to parse index: ");
			return FALSE;
		}
	}

	/* v4l devices are weird in that the vendor and model are generic */
	usb_device = fu_device_get_backend_parent_with_subsystem(device, "usb:usb_device", NULL);
	if (usb_device != NULL) {
		if (!fu_v4l_device_usb_probe(self, usb_device, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_v4l_device_setup(FuDevice *device, GError **error)
{
#ifdef HAVE_VIDEODEV2_H
	FuV4lDevice *self = FU_V4L_DEVICE(device);
	FuV4lDevicePrivate *priv = GET_PRIVATE(self);
	struct v4l2_capability v2cap = {0};

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  VIDIOC_QUERYCAP,
				  (guint8 *)&v2cap,
				  sizeof(v2cap),
				  NULL,
				  50, /* ms */
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error))
		return FALSE;
	if (v2cap.capabilities & V4L2_CAP_DEVICE_CAPS)
		priv->caps = v2cap.device_caps;
	else
		priv->caps = v2cap.capabilities;
#endif
	/* success */
	return TRUE;
}

static void
fu_v4l_device_incorporate(FuDevice *device, FuDevice *donor)
{
	FuV4lDevice *self = FU_V4L_DEVICE(device);
	FuV4lDevicePrivate *priv = GET_PRIVATE(self);
	priv->index = fu_v4l_device_get_index(FU_V4L_DEVICE(donor));
	priv->caps = fu_v4l_device_get_caps(FU_V4L_DEVICE(donor));
}

static void
fu_v4l_device_init(FuV4lDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
}

static void
fu_v4l_device_class_init(FuV4lDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_v4l_device_probe;
	device_class->setup = fu_v4l_device_setup;
	device_class->to_string = fu_v4l_device_to_string;
	device_class->incorporate = fu_v4l_device_incorporate;
}
