/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-runtime.h"
#include "fu-logitech-hidpp-struct.h"

typedef struct {
	guint8 version_bl_major;
	FuIOChannel *io_channel;
} FuLogitechHidppRuntimePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuLogitechHidppRuntime, fu_logitech_hidpp_runtime, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_logitech_hidpp_runtime_get_instance_private(o))

FuIOChannel *
fu_logitech_hidpp_runtime_get_io_channel(FuLogitechHidppRuntime *self)
{
	FuLogitechHidppRuntimePrivate *priv;
	g_return_val_if_fail(FU_IS_HIDPP_RUNTIME(self), NULL);
	priv = GET_PRIVATE(self);
	return priv->io_channel;
}

guint8
fu_logitech_hidpp_runtime_get_version_bl_major(FuLogitechHidppRuntime *self)
{
	FuLogitechHidppRuntimePrivate *priv;
	g_return_val_if_fail(FU_IS_HIDPP_RUNTIME(self), 0);
	priv = GET_PRIVATE(self);
	return priv->version_bl_major;
}

gboolean
fu_logitech_hidpp_runtime_enable_notifications(FuLogitechHidppRuntime *self, GError **error)
{
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	FuLogitechHidppRuntimePrivate *priv = GET_PRIVATE(self);

	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
	msg->sub_id = FU_LOGITECH_HIDPP_SUBID_SET_REGISTER;
	msg->function_id = FU_LOGITECH_HIDPP_REGISTER_HIDPP_NOTIFICATIONS;
	msg->data[0] = 0x00;
	msg->data[1] = 0x05; /* Wireless + SoftwarePresent */
	msg->data[2] = 0x00;
	msg->hidpp_version = 1;
	return fu_logitech_hidpp_transfer(priv->io_channel, msg, error);
}

static gboolean
fu_logitech_hidpp_runtime_close(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimePrivate *priv = GET_PRIVATE(self);

	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown(priv->io_channel, error))
			return FALSE;
		g_clear_object(&priv->io_channel);
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_open(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimePrivate *priv = GET_PRIVATE(self);
	const gchar *devpath = fu_linux_device_get_device_file(FU_LINUX_DEVICE(device));
	g_autoptr(FuIOChannel) io_channel = NULL;

	/* open, but don't block */
	io_channel = fu_io_channel_new_file(devpath, error);
	if (io_channel == NULL)
		return FALSE;
	g_set_object(&priv->io_channel, io_channel);

	/* poll for notifications */
	fu_device_set_poll_interval(device, FU_HIDPP_RECEIVER_RUNTIME_POLLING_INTERVAL);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_probe(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimePrivate *priv = GET_PRIVATE(self);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	guint16 release = 0xffff;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(GUdevDevice) udev_parent_usb_interface = NULL;

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "usb", error))
		return FALSE;

	/* generate bootloader-specific GUID */
	udev_parent = g_udev_device_get_parent_with_subsystem(udev_device, "usb", "usb_device");
	if (udev_parent != NULL) {
		const gchar *release_str;
		release_str = g_udev_device_get_property(udev_parent, "ID_REVISION");
		if (release_str != NULL)
			release = g_ascii_strtoull(release_str, NULL, 16);
	}
	if (release != 0xffff) {
		g_autofree gchar *devid2 = NULL;
		const gchar *interface_str;
		switch (release &= 0xff00) {
		case 0x1200:
			/* Nordic */
			devid2 =
			    g_strdup_printf("USB\\VID_%04X&PID_%04X",
					    (guint)FU_LOGITECH_HIDPP_DEVICE_VID,
					    (guint)FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_NORDIC);
			fu_device_add_counterpart_guid(device, devid2);
			priv->version_bl_major = 0x01;
			break;
		case 0x2400:
			/* Texas */
			devid2 =
			    g_strdup_printf("USB\\VID_%04X&PID_%04X",
					    (guint)FU_LOGITECH_HIDPP_DEVICE_VID,
					    (guint)FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_TEXAS);
			fu_device_add_counterpart_guid(device, devid2);
			priv->version_bl_major = 0x03;
			break;
		case 0x0500:
			/* Bolt */
			udev_parent_usb_interface =
			    g_udev_device_get_parent_with_subsystem(udev_device,
								    "usb",
								    "usb_interface");
			interface_str =
			    g_udev_device_get_property(udev_parent_usb_interface, "INTERFACE");
			if (interface_str == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "INTERFACE property not found in parent device");
				return FALSE;
			}
			if (g_strcmp0(interface_str, "3/0/0") != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "skipping hidraw device");
				return FALSE;
			}
			devid2 =
			    g_strdup_printf("USB\\VID_%04X&PID_%04X",
					    (guint)FU_LOGITECH_HIDPP_DEVICE_VID,
					    (guint)FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_BOLT);
			fu_device_add_counterpart_guid(device, devid2);
			priv->version_bl_major = 0x03;
			break;
		default:
			g_warning("bootloader release %04x invalid", release);
			break;
		}
	}
	return TRUE;
}

static void
fu_logitech_hidpp_runtime_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_logitech_hidpp_runtime_parent_class)->finalize(object);
}

static void
fu_logitech_hidpp_runtime_class_init(FuLogitechHidppRuntimeClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logitech_hidpp_runtime_finalize;
	device_class->open = fu_logitech_hidpp_runtime_open;
	device_class->probe = fu_logitech_hidpp_runtime_probe;
	device_class->close = fu_logitech_hidpp_runtime_close;
}

static void
fu_logitech_hidpp_runtime_init(FuLogitechHidppRuntime *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_icon(FU_DEVICE(self), "usb-receiver");
	fu_device_set_name(FU_DEVICE(self), "Unifying Receiver");
	fu_device_set_summary(FU_DEVICE(self), "Miniaturised USB wireless receiver");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}
