/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <appstream-glib.h>

#include "fu-nitrokey-common.h"
#include "fu-nitrokey-device.h"

G_DEFINE_TYPE (FuNitrokeyDevice, fu_nitrokey_device, FU_TYPE_USB_DEVICE)

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
	guint8 buf[64];

	g_return_val_if_fail (buf_in_sz <= NITROKEY_REQUEST_DATA_LENGTH, FALSE);
	g_return_val_if_fail (buf_out_sz <= NITROKEY_REPLY_DATA_LENGTH, FALSE);

	/* create the request */
	memset (buf, 0x00, sizeof(buf));
	buf[0] = command;
	if (buf_in != NULL)
		memcpy (&buf[1], buf_in, buf_in_sz);
	crc_tmp = fu_nitrokey_perform_crc32 (buf, sizeof(buf) - 4);
	fu_common_write_uint32 (&buf[NITROKEY_REQUEST_DATA_LENGTH + 1], crc_tmp, G_LITTLE_ENDIAN);

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

static gboolean
fu_nitrokey_device_probe (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* not the right kind of device */
	if (g_usb_device_get_vid (usb_device) != 0x20a0 ||
	    g_usb_device_get_pid (usb_device) != 0x4109) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported with this device");
		return FALSE;
	}

	/* harcoded */
	fu_device_set_name (FU_DEVICE (device), "Nitrokey Storage");
	fu_device_set_vendor (FU_DEVICE (device), "Nitrokey");
	fu_device_set_summary (FU_DEVICE (device), "A secure memory stick");
	fu_device_add_icon (FU_DEVICE (device), "media-removable");

	/* also add the USB VID:PID hash of the bootloader */
	fu_device_add_guid (FU_DEVICE (device), "USB\\VID_03EB&PID_2FF1");
	fu_device_set_remove_delay (FU_DEVICE (device), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);

	/* success */
	return TRUE;
}

static gboolean
fu_nitrokey_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	NitrokeyGetDeviceStatusPayload payload;
	guint8 buf_reply[NITROKEY_REPLY_DATA_LENGTH];
	g_autofree gchar *version = NULL;

	/* claim interface */
	if (!g_usb_device_claim_interface (usb_device, 0x02, /* idx */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to do claim nitrokey: ");
		return FALSE;
	}

	/* get firmware version */
	if (!nitrokey_execute_cmd_full (usb_device,
					NITROKEY_CMD_GET_DEVICE_STATUS,
					NULL, 0,
					buf_reply, sizeof(buf_reply),
					NULL, error)) {
		g_prefix_error (error, "failed to do get firmware version: ");
		return FALSE;
	}
	_dump_to_console ("payload", buf_reply, sizeof(buf_reply));
	memcpy (&payload, buf_reply, sizeof(buf_reply));
	version = g_strdup_printf ("%u.%u", payload.VersionMinor, payload.VersionMajor);
	fu_device_set_version (FU_DEVICE (device), version);

	/* success */
	return TRUE;
}

static gboolean
fu_nitrokey_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	g_autoptr(GError) error_local = NULL;

	/* reconnect kernel driver */
	if (!g_usb_device_release_interface (usb_device, 0x02,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     &error_local)) {
		g_warning ("failed to release interface: %s", error_local->message);
	}
	return TRUE;
}

static void
fu_nitrokey_device_init (FuNitrokeyDevice *device)
{
}

static void
fu_nitrokey_device_class_init (FuNitrokeyDeviceClass *klass)
{
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_usb_device->open = fu_nitrokey_device_open;
	klass_usb_device->close = fu_nitrokey_device_close;
	klass_usb_device->probe = fu_nitrokey_device_probe;
}

FuNitrokeyDevice *
fu_nitrokey_device_new (GUsbDevice *usb_device)
{
	FuNitrokeyDevice *device;
	device = g_object_new (FU_TYPE_NITROKEY_DEVICE,
			       "usb-device", usb_device,
			       NULL);
	return FU_NITROKEY_DEVICE (device);
}
