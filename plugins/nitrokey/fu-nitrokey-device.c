/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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
#include <appstream-glib.h>

#include "fu-nitrokey-common.h"
#include "fu-nitrokey-device.h"

typedef struct
{
	GUsbDevice		*usb_device;
	FuDeviceLocker		*usb_device_locker;
} FuNitrokeyDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuNitrokeyDevice, fu_nitrokey_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_nitrokey_device_get_instance_private (o))

#define NITROKEY_TRANSACTION_TIMEOUT		100 /* ms */
#define NITROKEY_NR_RETRIES			5

#define NITROKEY_REQUEST_DATA_LENGTH		59
#define NITROKEY_REPLY_DATA_LENGTH		53

#define NITROKEY_CMD_GET_DEVICE_STATUS		(0x20 + 14)

typedef struct __attribute__((packed)) {
	guint8		command;
	guint8		payload[NITROKEY_REQUEST_DATA_LENGTH];
	guint32		crc;
} NitrokeyHidRequest;

typedef struct __attribute__((packed)) {
	guint8		_padding; /* always zero */
	guint8		device_status;
	guint32		last_command_crc;
	guint8		last_command_status;
	guint8		payload[NITROKEY_REPLY_DATA_LENGTH];
	guint32		crc;
} NitrokeyHidResponse;

/* based from libnitrokey/stick20_commands.h */
typedef struct __attribute__((packed)) {
	guint8		_padding[24];
	guint8		SendCounter;
	guint8		SendDataType;
	guint8		FollowBytesFlag;
	guint8		SendSize;
	guint16		MagicNumber_StickConfig;
	guint8		ReadWriteFlagUncryptedVolume;
	guint8		ReadWriteFlagCryptedVolume;
	guint8		VersionReserved1;
	guint8		VersionMinor;
	guint8		VersionReserved2;
	guint8		VersionMajor;
	guint8		ReadWriteFlagHiddenVolume;
	guint8		FirmwareLocked;
	guint8		NewSDCardFound;
	guint8		SDFillWithRandomChars;
	guint32		ActiveSD_CardID;
	guint8		VolumeActiceFlag;
	guint8		NewSmartCardFound;
	guint8		UserPwRetryCount;
	guint8		AdminPwRetryCount;
	guint32		ActiveSmartCardID;
	guint8		StickKeysNotInitiated;
} NitrokeyGetDeviceStatusPayload;

static void
fu_nitrokey_device_finalize (GObject *object)
{
	FuNitrokeyDevice *device = FU_NITROKEY_DEVICE (object);
	FuNitrokeyDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->usb_device_locker != NULL)
		g_object_unref (priv->usb_device_locker);
	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);

	G_OBJECT_CLASS (fu_nitrokey_device_parent_class)->finalize (object);
}

static void
fu_nitrokey_device_init (FuNitrokeyDevice *device)
{
}

static void
fu_nitrokey_device_class_init (FuNitrokeyDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_nitrokey_device_finalize;
}


static void
_dump_to_console (const gchar *title, const guint8 *buf, gsize buf_sz)
{
	if (g_getenv ("FWUPD_NITROKEY_VERBOSE") == NULL)
		return;
	g_debug ("%s", title);
	for (gsize i = 0; i < buf_sz; i++)
		g_debug ("%" G_GSIZE_FORMAT "=0x%02x", i, buf[i]);
}

static gboolean
nitrokey_execute_cmd (GUsbDevice *usb_device, guint8 command,
		      const guint8 *buf_in, gsize buf_in_sz,
		      guint8 *buf_out, gsize buf_out_sz,
		      GCancellable *cancellable, GError **error)
{
	NitrokeyHidResponse res;
	gboolean ret;
	gsize actual_len = 0;
	guint32 crc_tmp;
	guint32 crc_le;
	guint8 buf[64];

	g_return_val_if_fail (buf_in_sz <= NITROKEY_REQUEST_DATA_LENGTH, FALSE);
	g_return_val_if_fail (buf_out_sz <= NITROKEY_REPLY_DATA_LENGTH, FALSE);

	/* create the request */
	memset (buf, 0x00, sizeof(buf));
	buf[0] = command;
	if (buf_in != NULL)
		memcpy (&buf[1], buf_in, buf_in_sz);
	crc_tmp = fu_nitrokey_perform_crc32 (buf, sizeof(buf) - 4);
	crc_le = GUINT32_TO_LE (crc_tmp);
	memcpy (&buf[NITROKEY_REQUEST_DATA_LENGTH + 1], &crc_le, sizeof(guint32));

	/* send request */
	_dump_to_console ("request", buf, sizeof(buf));
	ret = g_usb_device_control_transfer (usb_device,
					     G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					     G_USB_DEVICE_REQUEST_TYPE_CLASS,
					     G_USB_DEVICE_RECIPIENT_INTERFACE,
					     0x09, 0x0300, 0x0002,
					     buf, sizeof(buf),
					     &actual_len,
					     NITROKEY_TRANSACTION_TIMEOUT,
					     NULL, error);
	if (!ret) {
		g_prefix_error (error, "failed to do HOST_TO_DEVICE: ");
		return FALSE;
	}
	if (actual_len != sizeof(buf)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}

	/* get response */
	memset (buf, 0x00, sizeof(buf));
	ret = g_usb_device_control_transfer (usb_device,
					     G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					     G_USB_DEVICE_REQUEST_TYPE_CLASS,
					     G_USB_DEVICE_RECIPIENT_INTERFACE,
					     0x01, 0x0300, 0x0002,
					     buf, sizeof(buf),
					     &actual_len,
					     NITROKEY_TRANSACTION_TIMEOUT,
					     NULL,
					     error);
	if (!ret) {
		g_prefix_error (error, "failed to do DEVICE_TO_HOST: ");
		return FALSE;
	}
	if (actual_len != sizeof(res)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	_dump_to_console ("response", buf, sizeof(buf));

	/* verify this is the answer to the question we asked */
	memcpy (&res, buf, sizeof(buf));
	if (GUINT32_FROM_LE (res.last_command_crc) != crc_tmp) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "got response CRC %x, expected %x",
			     GUINT32_FROM_LE (res.last_command_crc), crc_tmp);
		return FALSE;
	}

	/* verify the response checksum */
	crc_tmp = fu_nitrokey_perform_crc32 (buf, sizeof(res) - 4);
	if (GUINT32_FROM_LE (res.crc) != crc_tmp) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "got packet CRC %x, expected %x",
			     GUINT32_FROM_LE (res.crc), crc_tmp);
		return FALSE;
	}

	/* copy out the payload */
	if (buf_out != NULL)
		memcpy (buf_out, &res.payload, buf_out_sz);

	/* success */
	return TRUE;
}

static gboolean
nitrokey_execute_cmd_full (GUsbDevice *usb_device, guint8 command,
			   const guint8 *buf_in, gsize buf_in_sz,
			   guint8 *buf_out, gsize buf_out_sz,
			   GCancellable *cancellable, GError **error)
{
	for (guint i = 0; i < NITROKEY_NR_RETRIES; i++) {
		g_autoptr(GError) error_local = NULL;
		gboolean ret;
		ret = nitrokey_execute_cmd (usb_device, command,
					    buf_in, buf_in_sz,
					    buf_out, buf_out_sz,
					    cancellable, &error_local);
		if (ret)
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_FAILED)) {
			if (error != NULL)
				*error = g_steal_pointer (&error_local);
			return FALSE;
		}
		g_warning ("retrying command: %s", error_local->message);
		g_usleep (100 * 1000);
	}

	/* failed */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		     "failed to issue command after %i retries",
		     NITROKEY_NR_RETRIES);
	return FALSE;
}

gboolean
fu_nitrokey_device_open (FuNitrokeyDevice *device, GError **error)
{
	FuNitrokeyDevicePrivate *priv = GET_PRIVATE (device);
	g_autofree gchar *vendor_id = NULL;
	NitrokeyGetDeviceStatusPayload payload;
	const gchar *platform_id = NULL;
	guint8 buf_reply[NITROKEY_REPLY_DATA_LENGTH];
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *platform_id_fixed = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* already open */
	if (priv->usb_device_locker != NULL)
		return TRUE;

	/* open, then ensure this is actually 8Bitdo hardware */
	locker = fu_device_locker_new (priv->usb_device, error);
	if (locker == NULL)
		return FALSE;
	if (!g_usb_device_claim_interface (priv->usb_device, 0x02, /* idx */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to do claim nitrokey: ");
		return FALSE;
	}

	/* get firmware version */
	if (!nitrokey_execute_cmd_full (priv->usb_device,
					NITROKEY_CMD_GET_DEVICE_STATUS,
					NULL, 0,
					buf_reply, sizeof(buf_reply),
					NULL, error)) {
		g_prefix_error (error, "failed to do get firmware version: ");
		return FALSE;
	}
	_dump_to_console ("payload", buf_reply, sizeof(buf_reply));
	memcpy (&payload, buf_reply, sizeof(buf_reply));

	/* we use a modified version of the platform ID so we can have multiple
	 * FuDeviceItems for the same device -- when we can have the same device
	 * handled by multiple plugins this won't be required... */
	platform_id = g_usb_device_get_platform_id (priv->usb_device);
	platform_id_fixed = g_strdup_printf ("%s_workaround", platform_id);
	fu_device_set_id (FU_DEVICE (device), platform_id_fixed);
	fu_device_set_name (FU_DEVICE (device), "Nitrokey Storage");
	fu_device_set_vendor (FU_DEVICE (device), "Nitrokey");
	fu_device_set_summary (FU_DEVICE (device), "A secure memory stick");
	fu_device_add_icon (FU_DEVICE (device), "media-removable");
	version = g_strdup_printf ("%u.%u", payload.VersionMinor, payload.VersionMajor);
	fu_device_set_version (FU_DEVICE (device), version);

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (priv->usb_device));
	fu_device_set_vendor_id (FU_DEVICE (device), vendor_id);

	/* use the USB VID:PID hash */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device));
	fu_device_add_guid (FU_DEVICE (device), devid1);

	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

	/* success */
	priv->usb_device_locker = g_steal_pointer (&locker);
	return TRUE;
}

gboolean
fu_nitrokey_device_close (FuNitrokeyDevice *device, GError **error)
{
	FuNitrokeyDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GError) error_local = NULL;

	/* reconnect kernel driver */
	if (!g_usb_device_release_interface (priv->usb_device, 0x02,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     &error_local)) {
		g_warning ("failed to release interface: %s", error_local->message);
	}

	g_clear_object (&priv->usb_device_locker);
	return TRUE;
}

static void
fu_nitrokey_device_set_usb_device (FuNitrokeyDevice *device, GUsbDevice *usb_device)
{
	FuNitrokeyDevicePrivate *priv = GET_PRIVATE (device);
	g_set_object (&priv->usb_device, usb_device);
}

FuNitrokeyDevice *
fu_nitrokey_device_new (GUsbDevice *usb_device)
{
	g_autoptr(FuNitrokeyDevice) device = NULL;
	device = g_object_new (FU_TYPE_NITROKEY_DEVICE, NULL);
	fu_nitrokey_device_set_usb_device (device, usb_device);
	return g_steal_pointer (&device);
}
