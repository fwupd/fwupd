/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)

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
	lu_device_add_guid (device, devid1);

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
			lu_device_add_guid (device, devid2);
			version_bl_major = 0x01;
			break;
		case 0x2400:
			/* Texas */
			devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
						  (guint) LU_DEVICE_VID,
						  (guint) LU_DEVICE_PID_BOOTLOADER_TEXAS);
			lu_device_add_guid (device, devid2);
			version_bl_major = 0x03;
			break;
		default:
			g_warning ("bootloader release %04x invalid", release);
			break;
		}
	}

	/* read all 10 bytes of the version register */
	memset (config, 0x00, sizeof (config));
	for (guint i = 0; i < 0x05; i++) {
		g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_idx = HIDPP_RECEIVER_IDX;
		msg->sub_id = HIDPP_GET_REGISTER_REQ;
		msg->data[0] = HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION;
		msg->data[1] = i;
		msg->len = 0x4;
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to read device config: ");
			return FALSE;
		}
		memcpy (config + (i * 2), msg->data + 2, 2);
	}

	/* logitech sends base 16 and then pads as if base 10... */
	version_fw = lu_format_version (config[2],
					config[3],
					(guint16) config[4] << 8 |
					config[5]);
	lu_device_set_version_fw (device, version_fw);

	/* get bootloader version */
	if (version_bl_major > 0) {
		version_bl = lu_format_version (version_bl_major,
						config[8],
						config[9]);
		lu_device_set_version_bl (device, version_bl);

		/* is the dongle expecting signed firmware */
		if ((version_bl_major == 0x01 && config[8] >= 0x04) ||
		    (version_bl_major == 0x03 && config[8] >= 0x02)) {
			lu_device_add_flag (device, LU_DEVICE_FLAG_SIGNED_FIRMWARE);
		}
	}

	/* we can flash this */
	lu_device_add_flag (device, LU_DEVICE_FLAG_CAN_FLASH);

	/* only the bootloader can do the update */
	lu_device_set_product (device, "Unifying Reciever");

	return TRUE;
}

static gboolean
lu_device_runtime_detach (LuDevice *device, GError **error)
{
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_idx = HIDPP_RECEIVER_IDX;
	msg->sub_id = HIDPP_SET_REGISTER_REQ;
	msg->data[0] = HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE;
	msg->data[1] = 'I';
	msg->data[2] = 'C';
	msg->data[3] = 'P';
	msg->len = 0x4;
	if (!lu_device_hidpp_send (device, msg, error)) {
		g_prefix_error (error, "failed to detach to bootloader: ");
		return FALSE;
	}
	return TRUE;
}

static void
lu_device_runtime_class_init (LuDeviceRuntimeClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	klass_device->open = lu_device_runtime_open;
	klass_device->detach = lu_device_runtime_detach;
}

static void
lu_device_runtime_init (LuDeviceRuntime *device)
{
}
