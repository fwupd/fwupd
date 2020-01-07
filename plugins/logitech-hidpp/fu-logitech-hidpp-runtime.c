/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-runtime.h"
#include "fu-logitech-hidpp-hidpp.h"

struct _FuLogitechHidPpRuntime
{
	FuUdevDevice		 parent_instance;
	guint8			 version_bl_major;
	gboolean		 signed_firmware;
	FuIOChannel		*io_channel;
};

G_DEFINE_TYPE (FuLogitechHidPpRuntime, fu_logitech_hidpp_runtime, FU_TYPE_UDEV_DEVICE)

static void
fu_logitech_hidpp_runtime_to_string (FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	fu_common_string_append_kb (str, idt, "SignedFirmware", self->signed_firmware);
}

static gboolean
fu_logitech_hidpp_runtime_enable_notifications (FuLogitechHidPpRuntime *self, GError **error)
{
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
	msg->sub_id = HIDPP_SUBID_SET_REGISTER;
	msg->function_id = HIDPP_REGISTER_HIDPP_NOTIFICATIONS;
	msg->data[0] = 0x00;
	msg->data[1] = 0x05; /* Wireless + SoftwarePresent */
	msg->data[2] = 0x00;
	msg->hidpp_version = 1;
	return fu_logitech_hidpp_transfer (self->io_channel, msg, error);
}

static gboolean
fu_logitech_hidpp_runtime_close (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	if (!fu_io_channel_shutdown (self->io_channel, error))
		return FALSE;
	g_clear_object (&self->io_channel);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_poll (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new (self, error);
	if (locker == NULL)
		return FALSE;

	/* is there any pending data to read */
	msg->hidpp_version = 1;
	if (!fu_logitech_hidpp_receive (self->io_channel, msg, timeout, &error_local)) {
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_TIMED_OUT)) {
			return TRUE;
		}
		g_warning ("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* HID++1.0 error */
	if (!fu_logitech_hidpp_msg_is_error (msg, &error_local)) {
		g_warning ("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* unifying receiver notification */
	if (msg->report_id == HIDPP_REPORT_ID_SHORT) {
		switch (msg->sub_id) {
		case HIDPP_SUBID_DEVICE_CONNECTION:
		case HIDPP_SUBID_DEVICE_DISCONNECTION:
		case HIDPP_SUBID_DEVICE_LOCKING_CHANGED:
			g_debug ("device connection event, do something");
			break;
		case HIDPP_SUBID_LINK_QUALITY:
			g_debug ("ignoring link quality message");
			break;
		case HIDPP_SUBID_ERROR_MSG:
			g_debug ("ignoring link quality message");
			break;
		default:
			g_debug ("unknown SubID %02x", msg->sub_id);
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_open (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));
	const gchar *devpath = g_udev_device_get_device_file (udev_device);

	/* open, but don't block */
	self->io_channel = fu_io_channel_new_file (devpath, error);
	if (self->io_channel == NULL)
		return FALSE;

	/* poll for notifications */
	fu_device_set_poll_interval (device, 5000);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_probe (FuUdevDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));
	guint16 release = 0xffff;
	g_autoptr(GUdevDevice) udev_parent = NULL;

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "usb", error))
		return FALSE;

	/* generate bootloader-specific GUID */
	udev_parent = g_udev_device_get_parent_with_subsystem (udev_device,
							       "usb", "usb_device");
	if (udev_parent != NULL) {
		const gchar *release_str;
		release_str = g_udev_device_get_property (udev_parent, "ID_REVISION");
		if (release_str != NULL)
			release = g_ascii_strtoull (release_str, NULL, 16);
	}
	if (release != 0xffff) {
		g_autofree gchar *devid2 = NULL;
		switch (release &= 0xff00) {
		case 0x1200:
			/* Nordic */
			devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
						  (guint) FU_UNIFYING_DEVICE_VID,
						  (guint) FU_UNIFYING_DEVICE_PID_BOOTLOADER_NORDIC);
			fu_device_add_counterpart_guid (FU_DEVICE (device), devid2);
			self->version_bl_major = 0x01;
			break;
		case 0x2400:
			/* Texas */
			devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
						  (guint) FU_UNIFYING_DEVICE_VID,
						  (guint) FU_UNIFYING_DEVICE_PID_BOOTLOADER_TEXAS);
			fu_device_add_counterpart_guid (FU_DEVICE (device), devid2);
			self->version_bl_major = 0x03;
			break;
		default:
			g_warning ("bootloader release %04x invalid", release);
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_setup_internal (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	guint8 config[10];
	g_autofree gchar *version_fw = NULL;

	/* read all 10 bytes of the version register */
	memset (config, 0x00, sizeof (config));
	for (guint i = 0x01; i < 0x05; i++) {
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

		/* workaround a bug in the 12.01 firmware, which fails with
		 * INVALID_VALUE when reading MCU1_HW_VERSION */
		if (i == 0x03)
			continue;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
		msg->sub_id = HIDPP_SUBID_GET_REGISTER;
		msg->function_id = HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION;
		msg->data[0] = i;
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_transfer (self->io_channel, msg, error)) {
			g_prefix_error (error, "failed to read device config: ");
			return FALSE;
		}
		if (!fu_memcpy_safe (config, sizeof(config), i * 2,	/* dst */
				     msg->data, sizeof(msg->data), 0x1,	/* src */
				     2, error))
			return FALSE;
	}

	/* get firmware version */
	version_fw = fu_logitech_hidpp_format_version ("RQR",
						 config[2],
						 config[3],
						 (guint16) config[4] << 8 |
						 config[5]);
	fu_device_set_version (device, version_fw, FWUPD_VERSION_FORMAT_PLAIN);

	/* get bootloader version */
	if (self->version_bl_major > 0) {
		g_autofree gchar *version_bl = NULL;
		version_bl = fu_logitech_hidpp_format_version ("BOT",
						self->version_bl_major,
						config[8],
						config[9]);
		fu_device_set_version_bootloader (FU_DEVICE (device), version_bl);

		/* is the dongle expecting signed firmware */
		if ((self->version_bl_major == 0x01 && config[8] >= 0x04) ||
		    (self->version_bl_major == 0x03 && config[8] >= 0x02)) {
			self->signed_firmware = TRUE;
		}
	}

	/* enable HID++ notifications */
	if (!fu_logitech_hidpp_runtime_enable_notifications (self, error)) {
		g_prefix_error (error, "failed to enable notifications: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_setup (FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	for (guint i = 0; i < 5; i++) {
		/* HID++1.0 devices have to sleep to allow Solaar to talk to
		 * the device first -- we can't use the SwID as this is a
		 * HID++2.0 feature */
		g_usleep (200*1000);
		if (fu_logitech_hidpp_runtime_setup_internal (device, &error_local))
			return TRUE;
		if (!g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA)) {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		g_clear_error (&error_local);
	}
	g_propagate_error (error, g_steal_pointer (&error_local));
	return FALSE;
}

static gboolean
fu_logitech_hidpp_runtime_detach (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_UNIFYING_RUNTIME (device);
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
	g_autoptr(GError) error_local = NULL;
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
	msg->sub_id = HIDPP_SUBID_SET_REGISTER;
	msg->function_id = HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE;
	msg->data[0] = 'I';
	msg->data[1] = 'C';
	msg->data[2] = 'P';
	msg->hidpp_version = 1;
	msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_send (self->io_channel, msg, FU_UNIFYING_DEVICE_TIMEOUT_MS, &error_local)) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE)) {
			g_debug ("failed to detach to bootloader: %s", error_local->message);
		} else {
			g_prefix_error (&error_local, "failed to detach to bootloader: ");
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_logitech_hidpp_runtime_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_logitech_hidpp_runtime_parent_class)->finalize (object);
}

static void
fu_logitech_hidpp_runtime_class_init (FuLogitechHidPpRuntimeClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_device_udev = FU_UDEV_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = fu_logitech_hidpp_runtime_finalize;
	klass_device->open = fu_logitech_hidpp_runtime_open;
	klass_device_udev->probe = fu_logitech_hidpp_runtime_probe;
	klass_device->setup = fu_logitech_hidpp_runtime_setup;
	klass_device->close = fu_logitech_hidpp_runtime_close;
	klass_device->detach = fu_logitech_hidpp_runtime_detach;
	klass_device->poll = fu_logitech_hidpp_runtime_poll;
	klass_device->to_string = fu_logitech_hidpp_runtime_to_string;
}

static void
fu_logitech_hidpp_runtime_init (FuLogitechHidPpRuntime *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_icon (FU_DEVICE (self), "preferences-desktop-keyboard");
	fu_device_set_name (FU_DEVICE (self), "Unifying Receiver");
	fu_device_set_summary (FU_DEVICE (self), "A miniaturised USB wireless receiver");
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_protocol (FU_DEVICE (self), "com.logitech.unifying");
}
