/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-nitrokey-common.h"
#include "fu-nitrokey-device.h"

G_DEFINE_TYPE (FuNitrokeyDevice, fu_nitrokey_device, FU_TYPE_USB_DEVICE)

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
	if (g_getenv ("FWUPD_NITROKEY_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "request", buf, sizeof(buf));
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
	if (g_getenv ("FWUPD_NITROKEY_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "response", buf, sizeof(buf));

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
fu_nitrokey_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* claim interface */
	if (!g_usb_device_claim_interface (usb_device, 0x02, /* idx */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to do claim nitrokey: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nitrokey_device_setup (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	NitrokeyGetDeviceStatusPayload payload;
	guint8 buf_reply[NITROKEY_REPLY_DATA_LENGTH];
	g_autofree gchar *version = NULL;

	/* get firmware version */
	if (!nitrokey_execute_cmd_full (usb_device,
					NITROKEY_CMD_GET_DEVICE_STATUS,
					NULL, 0,
					buf_reply, sizeof(buf_reply),
					NULL, error)) {
		g_prefix_error (error, "failed to do get firmware version: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_NITROKEY_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "payload", buf_reply, sizeof(buf_reply));
	memcpy (&payload, buf_reply, sizeof(payload));
	version = g_strdup_printf ("%u.%u", payload.VersionMajor, payload.VersionMinor);
	fu_device_set_version (FU_DEVICE (device), version, FWUPD_VERSION_FORMAT_PAIR);

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
	fu_device_set_remove_delay (FU_DEVICE (device), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_nitrokey_device_class_init (FuNitrokeyDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->setup = fu_nitrokey_device_setup;
	klass_usb_device->open = fu_nitrokey_device_open;
	klass_usb_device->close = fu_nitrokey_device_close;
}
