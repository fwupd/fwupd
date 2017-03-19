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
#include "lu-device-bootloader.h"
#include "lu-hidpp.h"

typedef struct
{
	guint16			 flash_addr_lo;
	guint16			 flash_addr_hi;
	guint16			 flash_blocksize;
} LuDeviceBootloaderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LuDeviceBootloader, lu_device_bootloader, LU_TYPE_DEVICE)

#define GET_PRIVATE(o) (lu_device_bootloader_get_instance_private (o))

LuDeviceBootloaderRequest *
lu_device_bootloader_request_new (void)
{
	LuDeviceBootloaderRequest *req = g_new0 (LuDeviceBootloaderRequest, 1);
	return req;
}

GPtrArray *
lu_device_bootloader_parse_requests (GBytes *fw, GError **error)
{
	const gchar *tmp;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GPtrArray) reqs = NULL;

	reqs = g_ptr_array_new_with_free_func (g_free);
	tmp = g_bytes_get_data (fw, NULL);
	lines = g_strsplit_set (tmp, "\n\r", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		LuDeviceBootloaderRequest *payload;

		/* skip empty lines */
		tmp = lines[i];
		if (strlen (tmp) < 5)
			continue;

		payload = lu_device_bootloader_request_new ();
		payload->len = lu_buffer_read_uint8 (tmp + 0x01);
		if (payload->len > 28) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware data invalid: too large %u bytes",
				     payload->len);
			return NULL;
		}
		payload->addr = ((guint16) lu_buffer_read_uint8 (tmp + 0x03)) << 8;
		payload->addr |= lu_buffer_read_uint8 (tmp + 0x05);

		/* read the data, but skip the checksum byte */
		for (guint j = 0; j < payload->len; j++) {
			const gchar *ptr = tmp + 0x09 + (j * 2);
			if (ptr[0] == '\0') {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "firmware data invalid: expected %u bytes",
					     payload->len);
				return NULL;
			}
			payload->data[j] = lu_buffer_read_uint8 (ptr);
		}
		g_ptr_array_add (reqs, payload);
	}
	return g_steal_pointer (&reqs);
}

guint16
lu_device_bootloader_get_addr_lo (LuDevice *device)
{
	LuDeviceBootloader *device_bootloader = LU_DEVICE_BOOTLOADER (device);
	LuDeviceBootloaderPrivate *priv = GET_PRIVATE (device_bootloader);
	g_return_val_if_fail (LU_IS_DEVICE (device), 0x0000);
	return priv->flash_addr_lo;
}

guint16
lu_device_bootloader_get_addr_hi (LuDevice *device)
{
	LuDeviceBootloader *device_bootloader = LU_DEVICE_BOOTLOADER (device);
	LuDeviceBootloaderPrivate *priv = GET_PRIVATE (device_bootloader);
	g_return_val_if_fail (LU_IS_DEVICE (device), 0x0000);
	return priv->flash_addr_hi;
}

guint16
lu_device_bootloader_get_blocksize (LuDevice *device)
{
	LuDeviceBootloader *device_bootloader = LU_DEVICE_BOOTLOADER (device);
	LuDeviceBootloaderPrivate *priv = GET_PRIVATE (device_bootloader);
	g_return_val_if_fail (LU_IS_DEVICE (device), 0x0000);
	return priv->flash_blocksize;
}

static gboolean
lu_device_bootloader_attach (LuDevice *device, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_REBOOT;
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to attach back to runtime: ");
		return FALSE;
	}
	return TRUE;
}

static guint16
cd_buffer_read_uint16_be (const guint8 *buffer)
{
	guint16 tmp;
	memcpy (&tmp, buffer, sizeof(tmp));
	return GUINT16_FROM_BE (tmp);
}

static gboolean
lu_device_bootloader_open (LuDevice *device, GError **error)
{
	LuDeviceBootloader *device_bootloader = LU_DEVICE_BOOTLOADER (device);
	LuDeviceBootloaderPrivate *priv = GET_PRIVATE (device_bootloader);
	g_autofree gchar *name = NULL;
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();

	/* generate name */
	name = g_strdup_printf ("Unifying [%s]",
				lu_device_kind_to_string (lu_device_get_kind (device)));
	lu_device_set_product (device, name);

	/* we can flash this */
	lu_device_add_flag (device, LU_DEVICE_FLAG_CAN_FLASH);

	/* get memory map */
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_GET_MEMINFO;
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to init fw transfer: ");
		return FALSE;
	}
	if (req->len != 0x06) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to get meminfo: invalid size %02x",
			     req->len);
		return FALSE;
	}

	/* parse values */
	priv->flash_addr_lo = cd_buffer_read_uint16_be (req->data + 0);
	priv->flash_addr_hi = cd_buffer_read_uint16_be (req->data + 2);
	priv->flash_blocksize = cd_buffer_read_uint16_be (req->data + 4);
	return TRUE;
}

static gboolean
lu_device_bootloader_close (LuDevice *device, GError **error)
{
	GUsbDevice *usb_device = lu_device_get_usb_device (device);
	if (usb_device != NULL) {
		if (!g_usb_device_release_interface (usb_device, 0x00,
						     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
						     error)) {
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
lu_device_send_request (LuDevice *device,
			guint16 value,
			guint16 idx,
			const guint8 *data_in,
			gsize data_in_length,
			guint8 *data_out,
			gsize data_out_length,
			guint8 endpoint,
			GError **error)
{
	GUsbDevice *usb_device = lu_device_get_usb_device (device);
	gsize actual_length = 0;
	guint8 buf[32];

	/* send request */
	lu_dump_raw ("host->device", data_in, data_in_length);
	if (usb_device != NULL) {
		g_autofree guint8 *data_in_buf = g_memdup (data_in, data_in_length);
		if (!g_usb_device_control_transfer (usb_device,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    LU_REQUEST_SET_REPORT,
						    value, idx,
						    data_in_buf, data_in_length,
						    &actual_length,
						    LU_DEVICE_TIMEOUT_MS,
						    NULL,
						    error)) {
			g_prefix_error (error, "failed to send data: ");
			return FALSE;
		}
	}

	/* get response */
	memset (buf, 0x00, sizeof (buf));
	if (usb_device != NULL) {
		if (!g_usb_device_interrupt_transfer (usb_device,
						      endpoint,
						      buf,
						      sizeof (buf),
						      &actual_length,
						      LU_DEVICE_TIMEOUT_MS,
						      NULL,
						      error)) {
			g_prefix_error (error, "failed to get data: ");
			return FALSE;
		}
	} else {
		/* emulated */
		buf[0] = data_in[0];
		if (buf[0] == LU_DEVICE_BOOTLOADER_CMD_GET_MEMINFO) {
			buf[3] = 0x06; /* len */
			buf[4] = 0x40; /* lo MSB */
			buf[5] = 0x00; /* lo LSB */
			buf[6] = 0x6b; /* hi MSB */
			buf[7] = 0xff; /* hi LSB */
			buf[8] = 0x00; /* bs MSB */
			buf[9] = 0x80; /* bs LSB */
		}
		actual_length = data_out_length;
	}
	lu_dump_raw ("device->host", buf, actual_length);

	/* check sizes */
	if (data_out != NULL) {
		if (actual_length > data_out_length) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "device output %" G_GSIZE_FORMAT " bytes, "
				     "buffer size only %" G_GSIZE_FORMAT,
				     actual_length, data_out_length);
			return FALSE;
		}
		memcpy (data_out, buf, actual_length);
	}

	return TRUE;
}

gboolean
lu_device_bootloader_request (LuDevice *device,
			      LuDeviceBootloaderRequest *req,
			      GError **error)
{
	guint8 buf[32];

	/* build packet */
	memset (buf, 0x00, sizeof (buf));
	buf[0x00] = req->cmd;
	buf[0x01] = req->addr >> 8;
	buf[0x02] = req->addr & 0xff;
	buf[0x03] = req->len;
	memcpy (buf + 0x04, req->data, 28);

	/* send request */
	if (!lu_device_send_request (device, 0x0200, 0x0000,
				     buf, sizeof (buf),
				     buf, sizeof (buf),
				     LU_DEVICE_EP1,
				     error)) {
		return FALSE;
	}

	/* parse response */
	if ((buf[0x00] & 0xf0) != req->cmd) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "invalid command response of %02x, expected %02x",
			     buf[0x00], req->cmd);
		return FALSE;
	}
	req->cmd = buf[0x00];
	req->addr = ((guint16) buf[0x01] << 8) + buf[0x02];
	req->len = buf[0x03];
	if (req->len > 28) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "invalid data size of %02x", req->len);
		return FALSE;
	}
	memset (req->data, 0x00, 28);
	if (req->len > 0)
		memcpy (req->data, buf + 0x04, req->len);
	return TRUE;
}

static void
lu_device_bootloader_init (LuDeviceBootloader *device)
{
}

static void
lu_device_bootloader_class_init (LuDeviceBootloaderClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	klass_device->attach = lu_device_bootloader_attach;
	klass_device->open = lu_device_bootloader_open;
	klass_device->close = lu_device_bootloader_close;
}
