/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "lu-common.h"
#include "lu-device-runtime.h"
#include "lu-hidpp.h"

struct _LuDeviceRuntime
{
	LuDevice	 parent_instance;
};

G_DEFINE_TYPE (LuDeviceRuntime, lu_device_runtime, LU_TYPE_DEVICE)

#ifndef HAVE_GUDEV_232
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#endif

static gboolean
lu_device_runtime_enable_notifications (LuDevice *device, GError **error)
{
	g_autoptr(LuHidppMsg) msg = lu_hidpp_msg_new ();
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = HIDPP_SUBID_SET_REGISTER;
	msg->function_id = HIDPP_REGISTER_HIDPP_NOTIFICATIONS;
	msg->data[0] = 0x00;
	msg->data[1] = 0x05; /* Wireless + SoftwarePresent */
	msg->data[2] = 0x00;
	return lu_device_hidpp_transfer (device, msg, error);
}

static gboolean
lu_device_runtime_open (LuDevice *device, GError **error)
{
	GUdevDevice *udev_device = lu_device_get_udev_device (device);
	GUsbDevice *usb_device = lu_device_get_usb_device (device);
	guint16 release = 0xffff;
	guint8 config[10];
	guint8 version_bl_major = 0;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *version_bl = NULL;
	g_autofree gchar *version_fw = NULL;

	/* add a generic GUID */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  (guint) LU_DEVICE_VID,
				  (guint) LU_DEVICE_PID_RUNTIME);
	fu_device_add_guid (FU_DEVICE (device), devid1);

	/* generate bootloadder-specific GUID */
	if (usb_device != NULL) {
		release = g_usb_device_get_release (usb_device);
	} else if (udev_device != NULL) {
		g_autoptr(GUdevDevice) udev_parent = NULL;
		udev_parent = g_udev_device_get_parent_with_subsystem (udev_device,
								       "usb", "usb_device");
		if (udev_parent != NULL) {
			const gchar *release_str;
			release_str = g_udev_device_get_property (udev_parent, "ID_REVISION");
			if (release_str != NULL)
				release = g_ascii_strtoull (release_str, NULL, 16);
		}
	}
	if (release != 0xffff) {
		g_autofree gchar *devid2 = NULL;
		switch (release &= 0xff00) {
		case 0x1200:
			/* Nordic */
			devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
						  (guint) LU_DEVICE_VID,
						  (guint) LU_DEVICE_PID_BOOTLOADER_NORDIC);
			fu_device_add_guid (FU_DEVICE (device), devid2);
			version_bl_major = 0x01;
			break;
		case 0x2400:
			/* Texas */
			devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
						  (guint) LU_DEVICE_VID,
						  (guint) LU_DEVICE_PID_BOOTLOADER_TEXAS);
			fu_device_add_guid (FU_DEVICE (device), devid2);
			version_bl_major = 0x03;
			break;
		default:
			g_warning ("bootloader release %04x invalid", release);
			break;
		}
	}

	/* read all 10 bytes of the version register */
	memset (config, 0x00, sizeof (config));
	for (guint i = 0x01; i < 0x05; i++) {
		g_autoptr(LuHidppMsg) msg = lu_hidpp_msg_new ();

		/* workaround a bug in the 12.01 firmware, which fails with
		 * INVALID_VALUE when reading MCU1_HW_VERSION */
		if (i == 0x03)
			continue;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = lu_device_get_hidpp_id (device);
		msg->sub_id = HIDPP_SUBID_GET_REGISTER;
		msg->function_id = HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION;
		msg->data[0] = i;
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to read device config: ");
			return FALSE;
		}
		memcpy (config + (i * 2), msg->data + 1, 2);
	}

	/* get firmware version */
	version_fw = lu_format_version ("RQR",
					config[2],
					config[3],
					(guint16) config[4] << 8 |
					config[5]);
	fu_device_set_version (FU_DEVICE (device), version_fw);

	/* get bootloader version */
	if (version_bl_major > 0) {
		version_bl = lu_format_version ("BOT",
						version_bl_major,
						config[8],
						config[9]);
		fu_device_set_version_bootloader (FU_DEVICE (device), version_bl);

		/* is the dongle expecting signed firmware */
		if ((version_bl_major == 0x01 && config[8] >= 0x04) ||
		    (version_bl_major == 0x03 && config[8] >= 0x02)) {
			lu_device_add_flag (device, LU_DEVICE_FLAG_REQUIRES_SIGNED_FIRMWARE);
		}
	}

	/* enable HID++ notifications */
	if (!lu_device_runtime_enable_notifications (device, error)) {
		g_prefix_error (error, "failed to enable notifications: ");
		return FALSE;
	}

	/* this only exists with the original HID++1.0 version */
	lu_device_set_hidpp_version (device, 1.f);

	/* we can flash this */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* only the bootloader can do the update */
	fu_device_set_name (FU_DEVICE (device), "Unifying Receiver");

	return TRUE;
}

static gboolean
lu_device_runtime_detach (LuDevice *device, GError **error)
{
	g_autoptr(LuHidppMsg) msg = lu_hidpp_msg_new ();
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = HIDPP_SUBID_SET_REGISTER;
	msg->function_id = HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE;
	msg->data[0] = 'I';
	msg->data[1] = 'C';
	msg->data[2] = 'P';
	msg->flags = LU_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!lu_device_hidpp_send (device, msg, LU_DEVICE_TIMEOUT_MS, error)) {
		g_prefix_error (error, "failed to detach to bootloader: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
lu_device_runtime_poll (LuDevice *device, GError **error)
{
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(LuHidppMsg) msg = lu_hidpp_msg_new ();

	/* is there any pending data to read */
	if (!lu_device_hidpp_receive (device, msg, timeout, &error_local)) {
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_TIMED_OUT)) {
			return TRUE;
		}
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to get pending read: %s",
			     error_local->message);
		return FALSE;
	}

	/* HID++1.0 error */
	if (!lu_hidpp_msg_is_error (msg, error))
		return FALSE;

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

static void
lu_device_runtime_class_init (LuDeviceRuntimeClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	klass_device->open = lu_device_runtime_open;
	klass_device->poll = lu_device_runtime_poll;
	klass_device->detach = lu_device_runtime_detach;
}

static void
lu_device_runtime_init (LuDeviceRuntime *device)
{
	/* FIXME: we need something better */
	fu_device_add_icon (FU_DEVICE (device), "preferences-desktop-keyboard");
	fu_device_set_summary (FU_DEVICE (device), "A miniaturised USB wireless receiver");
}
