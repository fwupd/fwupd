/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-runtime.h"

typedef struct {
	guint8 version_bl_major;
	FuIOChannel *io_channel;
} FuLogitechHidPpRuntimePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuLogitechHidPpRuntime, fu_logitech_hidpp_runtime, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_logitech_hidpp_runtime_get_instance_private(o))

FuIOChannel *
fu_logitech_hidpp_runtime_get_io_channel(FuLogitechHidPpRuntime *self)
{
	FuLogitechHidPpRuntimePrivate *priv;
	g_return_val_if_fail(FU_IS_HIDPP_RUNTIME(self), NULL);
	priv = GET_PRIVATE(self);
	return priv->io_channel;
}

void
fu_logitech_hidpp_runtime_set_io_channel(FuLogitechHidPpRuntime *self, FuIOChannel *io_channel)
{
	FuLogitechHidPpRuntimePrivate *priv;
	g_return_if_fail(FU_IS_HIDPP_RUNTIME(self));
	priv = GET_PRIVATE(self);
	priv->io_channel = io_channel;
}

guint8
fu_logitech_hidpp_runtime_get_version_bl_major(FuLogitechHidPpRuntime *self)
{
	FuLogitechHidPpRuntimePrivate *priv;
	g_return_val_if_fail(FU_IS_HIDPP_RUNTIME(self), 0);
	priv = GET_PRIVATE(self);
	return priv->version_bl_major;
}

void
fu_logitech_hidpp_runtime_set_version_bl_major(FuLogitechHidPpRuntime *self,
					       guint8 version_bl_major)
{
	FuLogitechHidPpRuntimePrivate *priv;
	g_return_if_fail(FU_IS_HIDPP_RUNTIME(self));
	priv = GET_PRIVATE(self);
	priv->version_bl_major = version_bl_major;
}

static void
fu_logitech_hidpp_runtime_to_string(FuDevice *device, guint idt, GString *str)
{
	FU_DEVICE_CLASS(fu_logitech_hidpp_runtime_parent_class)->to_string(device, idt, str);
}

gboolean
fu_logitech_hidpp_runtime_enable_notifications(FuLogitechHidPpRuntime *self, GError **error)
{
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	FuLogitechHidPpRuntimePrivate *priv = GET_PRIVATE(self);

	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = HIDPP_DEVICE_IDX_RECEIVER;
	msg->sub_id = HIDPP_SUBID_SET_REGISTER;
	msg->function_id = HIDPP_REGISTER_HIDPP_NOTIFICATIONS;
	msg->data[0] = 0x00;
	msg->data[1] = 0x05; /* Wireless + SoftwarePresent */
	msg->data[2] = 0x00;
	msg->hidpp_version = 1;
	return fu_logitech_hidpp_transfer(priv->io_channel, msg, error);
}

static gboolean
fu_logitech_hidpp_runtime_close(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidPpRuntimePrivate *priv = GET_PRIVATE(self);

	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown(priv->io_channel, error))
			return FALSE;
		g_clear_object(&priv->io_channel);
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_poll(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidPpRuntimePrivate *priv = GET_PRIVATE(self);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(self, error);
	if (locker == NULL)
		return FALSE;

	/* is there any pending data to read */
	msg->hidpp_version = 1;
	if (!fu_logitech_hidpp_receive(priv->io_channel, msg, timeout, &error_local)) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
			return TRUE;
		}
		g_warning("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* HID++1.0 error */
	if (!fu_logitech_hidpp_msg_is_error(msg, &error_local)) {
		g_warning("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* unifying receiver notification */
	if (msg->report_id == HIDPP_REPORT_ID_SHORT) {
		switch (msg->sub_id) {
		case HIDPP_SUBID_DEVICE_CONNECTION:
		case HIDPP_SUBID_DEVICE_DISCONNECTION:
		case HIDPP_SUBID_DEVICE_LOCKING_CHANGED:
			g_debug("device connection event, do something");
			break;
		case HIDPP_SUBID_LINK_QUALITY:
			g_debug("ignoring link quality message");
			break;
		case HIDPP_SUBID_ERROR_MSG:
			g_debug("ignoring link quality message");
			break;
		default:
			g_debug("unknown SubID %02x", msg->sub_id);
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_open(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidPpRuntimePrivate *priv = GET_PRIVATE(self);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	const gchar *devpath = g_udev_device_get_device_file(udev_device);

	/* open, but don't block */
	priv->io_channel = fu_io_channel_new_file(devpath, error);
	if (priv->io_channel == NULL)
		return FALSE;

	/* poll for notifications */
	fu_device_set_poll_interval(device, FU_HIDPP_RECEIVER_RUNTIME_POLLING_INTERVAL);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_probe(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidPpRuntimePrivate *priv = GET_PRIVATE(self);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	guint16 release = 0xffff;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(GUdevDevice) udev_parent_usb_interface = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_logitech_hidpp_runtime_parent_class)->probe(device, error))
		return FALSE;

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
			devid2 = g_strdup_printf("USB\\VID_%04X&PID_%04X",
						 (guint)FU_UNIFYING_DEVICE_VID,
						 (guint)FU_UNIFYING_DEVICE_PID_BOOTLOADER_NORDIC);
			fu_device_add_counterpart_guid(device, devid2);
			priv->version_bl_major = 0x01;
			break;
		case 0x2400:
			/* Texas */
			devid2 = g_strdup_printf("USB\\VID_%04X&PID_%04X",
						 (guint)FU_UNIFYING_DEVICE_VID,
						 (guint)FU_UNIFYING_DEVICE_PID_BOOTLOADER_TEXAS);
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
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
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
			devid2 = g_strdup_printf("USB\\VID_%04X&PID_%04X",
						 (guint)FU_UNIFYING_DEVICE_VID,
						 (guint)FU_UNIFYING_DEVICE_PID_BOOTLOADER_BOLT);
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
fu_logitech_hidpp_runtime_class_init(FuLogitechHidPpRuntimeClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logitech_hidpp_runtime_finalize;
	klass_device->open = fu_logitech_hidpp_runtime_open;
	klass_device->probe = fu_logitech_hidpp_runtime_probe;
	klass_device->close = fu_logitech_hidpp_runtime_close;
	klass_device->poll = fu_logitech_hidpp_runtime_poll;
	klass_device->to_string = fu_logitech_hidpp_runtime_to_string;
}

static void
fu_logitech_hidpp_runtime_init(FuLogitechHidPpRuntime *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_icon(FU_DEVICE(self), "usb-receiver");
	fu_device_set_name(FU_DEVICE(self), "Unifying Receiver");
	fu_device_set_summary(FU_DEVICE(self), "Miniaturised USB wireless receiver");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}
