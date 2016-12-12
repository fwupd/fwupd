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

#include <appstream-glib.h>
#include <string.h>

#include "unifying-dongle.h"

#define UNIFYING_REQUEST_SET_REPORT			0x09
#define UNIFYING_DONGLE_TIMEOUT_MS			2500
#define UNIFYING_DONGLE_EP1				0x81
#define UNIFYING_DONGLE_EP3				0x83

/* HID++ constants */
#define UNIFYING_HIDPP_DEVICE_INDEX_RECEIVER		0xff
#define UNIFYING_HIDPP_REPORT_ID_SHORT			0x10
#define UNIFYING_HIDPP_REPORT_ID_LONG			0x11
#define UNIFYING_HIDPP_REPORT_ID_MEDIUM			0x20
#define UNIFYING_HIDPP_SET_REGISTER_REQ			0x80
#define UNIFYING_HIDPP_GET_REGISTER_REQ			0x81

#define UNIFYING_HIDPP_REGISTER_ADDR_UNKNOWN_F0		0xf0
#define UNIFYING_HIDPP_REGISTER_ADDR_VERSION		0xf1

#define UNIFYING_FIRMWARE_SIZE				0x7000

typedef enum {
	UNIFYING_BOOTLOADER_CMD_PAYLOAD			= 0x20,
	UNIFYING_BOOTLOADER_CMD_ERASE_PAGE		= 0x30,
	UNIFYING_BOOTLOADER_CMD_REBOOT			= 0x70,
	UNIFYING_BOOTLOADER_CMD_INIT_TRANSFER		= 0x80,
	UNIFYING_BOOTLOADER_CMD_WRITE_PAGE		= 0xc0,
	UNIFYING_BOOTLOADER_CMD_SET_ADDRESS		= 0xd0,
	UNIFYING_BOOTLOADER_CMD_LAST
} UnifyingBootloaderCmd;

typedef struct
{
	UnifyingDongleKind	 kind;
	GUsbDevice		*usb_device;
	gchar			*guid;
	gchar			*version_firmware;
	gchar			*version_bootloader;
} UnifyingDonglePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (UnifyingDongle, unifying_dongle, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (unifying_dongle_get_instance_private (o))

static void
fu_unifying_dump_raw (const gchar *title, const guint8 *data, gsize len)
{
	g_autoptr(GString) str = g_string_new (NULL);
	if (len == 0)
		return;
	g_string_append_printf (str, "%s:", title);
	for (gsize i = strlen (title); i < 16; i++)
		g_string_append (str, " ");
	for (gsize i = 0; i < len; i++) {
		g_string_append_printf (str, "%02x ", data[i]);
		if (i > 0 && i % 32 == 0)
			g_string_append (str, "\n");
	}
	g_debug ("%s", str->str);
}

UnifyingDongleKind
unifying_dongle_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "runtime") == 0)
		return UNIFYING_DONGLE_KIND_RUNTIME;
	if (g_strcmp0 (kind, "bootloader-nordic") == 0)
		return UNIFYING_DONGLE_KIND_BOOTLOADER_NORDIC;
	if (g_strcmp0 (kind, "bootloader-texas") == 0)
		return UNIFYING_DONGLE_KIND_BOOTLOADER_TEXAS;
	return UNIFYING_DONGLE_KIND_UNKNOWN;
}

const gchar *
unifying_dongle_kind_to_string (UnifyingDongleKind kind)
{
	if (kind == UNIFYING_DONGLE_KIND_RUNTIME)
		return "runtime";
	if (kind == UNIFYING_DONGLE_KIND_BOOTLOADER_NORDIC)
		return "bootloader-nordic";
	if (kind == UNIFYING_DONGLE_KIND_BOOTLOADER_TEXAS)
		return "bootloader-texas";
	return NULL;
}

static void
unifying_dongle_finalize (GObject *object)
{
	UnifyingDongle *dongle = UNIFYING_DONGLE (object);
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);

	g_free (priv->guid);
	g_free (priv->version_firmware);
	g_free (priv->version_bootloader);
	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);

	G_OBJECT_CLASS (unifying_dongle_parent_class)->finalize (object);
}

static void
unifying_dongle_init (UnifyingDongle *dongle)
{
}

static void
unifying_dongle_class_init (UnifyingDongleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = unifying_dongle_finalize;
}

UnifyingDongleKind
unifying_dongle_get_kind (UnifyingDongle *dongle)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	return priv->kind;
}

GUsbDevice *
unifying_dongle_get_usb_device (UnifyingDongle *dongle)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	return priv->usb_device;
}

static gboolean
unifying_dongle_send_command (UnifyingDongle *dongle,
				 guint16 value,
				 guint16 idx,
				 const guint8 *data_in,
				 gsize data_in_length,
				 guint8 *data_out,
				 gsize data_out_length,
				 guint8 endpoint,
				 GError **error)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	gsize actual_length = 0;
	guint8 buf[32];

	/* send request */
	fu_unifying_dump_raw ("host->device", data_in, data_in_length);
	if (priv->usb_device != NULL &&
	    !g_usb_device_control_transfer (priv->usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    UNIFYING_REQUEST_SET_REPORT,
					    value, idx,
					    data_in, data_in_length,
					    &actual_length,
					    UNIFYING_DONGLE_TIMEOUT_MS,
					    NULL,
					    error)) {
		g_prefix_error (error, "failed to send data: ");
		return FALSE;
	}

	/* get response */
	memset (buf, 0x00, sizeof (buf));
	if (priv->usb_device != NULL) {
		if (!g_usb_device_interrupt_transfer (priv->usb_device,
						      endpoint,
						      buf,
						      sizeof (buf),
						      &actual_length,
						      UNIFYING_DONGLE_TIMEOUT_MS,
						      NULL,
						      error)) {
			g_prefix_error (error, "failed to get data: ");
			return FALSE;
		}
	} else {
		/* emulated */
		actual_length = data_out_length;
	}
	fu_unifying_dump_raw ("dongle->host", buf, actual_length);

	/* check sizes */
	if (data_out != NULL) {
		if (actual_length > data_out_length) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "dongle output %" G_GSIZE_FORMAT " bytes, "
				     "buffer size only %" G_GSIZE_FORMAT,
				     actual_length, data_out_length);
			return FALSE;
		}
		memcpy (data_out, buf, actual_length);
	}

	return TRUE;
}

gboolean
unifying_dongle_detach (UnifyingDongle *dongle, GError **error)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	const guint8 cmd[] =  { UNIFYING_HIDPP_REPORT_ID_SHORT,
				UNIFYING_HIDPP_DEVICE_INDEX_RECEIVER,
				UNIFYING_HIDPP_SET_REGISTER_REQ,
				UNIFYING_HIDPP_REGISTER_ADDR_UNKNOWN_F0,
				0x49, 0x43, 0x50 /* value */};

	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (priv->kind != UNIFYING_DONGLE_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "dongle is not in runtime state");
		return FALSE;
	}

	/* detach */
	fu_unifying_dump_raw ("host->device", cmd, sizeof (cmd));
	if (!g_usb_device_control_transfer (priv->usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    UNIFYING_REQUEST_SET_REPORT,
					    0x0210, 0x0002,
					    cmd, sizeof (cmd),
					    NULL,
					    UNIFYING_DONGLE_TIMEOUT_MS,
					    NULL,
					    error)) {
		g_prefix_error (error, "failed to detach to bootloader: ");
		return FALSE;
	}

	return TRUE;
}

gboolean
unifying_dongle_attach (UnifyingDongle *dongle, GError **error)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	guint8 cmd[32];

	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (priv->kind == UNIFYING_DONGLE_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "dongle is not in bootloader state");
		return FALSE;
	}

	/* attach */
	memset (cmd, 0x0, sizeof(cmd));
	cmd[0x0] = UNIFYING_BOOTLOADER_CMD_REBOOT;
	fu_unifying_dump_raw ("host->device", cmd, sizeof (cmd));
	if (!unifying_dongle_send_command (dongle,
					      0x0200, 0x0000,
					      cmd, sizeof (cmd),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to attach back to runtime: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
unifying_dongle_reset (UnifyingDongle *dongle, GError **error)
{
	const guint8 cmd[] =  { UNIFYING_HIDPP_REPORT_ID_SHORT,
				UNIFYING_HIDPP_DEVICE_INDEX_RECEIVER,
				UNIFYING_HIDPP_GET_REGISTER_REQ,
				UNIFYING_HIDPP_REGISTER_ADDR_VERSION,
				0x00, 0x00, 0x00 };
	if (!unifying_dongle_send_command (dongle, 0x0210, 0x0002,
					      cmd, sizeof (cmd),
					      NULL, 0,
					      UNIFYING_DONGLE_EP3,
					      error)) {
		g_prefix_error (error, "failed to reset");
		return FALSE;
	}
	return TRUE;
}

gboolean
unifying_dongle_open (UnifyingDongle *dongle, GError **error)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	guint i;
	guint num_interfaces = 0x1;
	g_autofree gchar *devid = NULL;

	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (priv->usb_device == NULL) {
		priv->version_firmware = g_strdup ("001.002.00003");
		priv->version_bootloader = g_strdup ("BL.004.005");
		return TRUE;
	}

	/* generate GUID -- in runtime mode we have to use the release */
	if (priv->kind == UNIFYING_DONGLE_KIND_RUNTIME) {
		guint16 release = g_usb_device_get_release (priv->usb_device);
		release &= 0xff00;
		devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
					 g_usb_device_get_vid (priv->usb_device),
					 g_usb_device_get_pid (priv->usb_device),
					 release);
	} else {
		devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
					 g_usb_device_get_vid (priv->usb_device),
					 g_usb_device_get_pid (priv->usb_device));
	}
	g_debug ("Using %s for GUID", devid);
	priv->guid = as_utils_guid_from_string (devid);

	/* open device */
	g_debug ("opening unifying device");
	if (!g_usb_device_open (priv->usb_device, error))
		return FALSE;
	if (priv->kind == UNIFYING_DONGLE_KIND_RUNTIME)
		num_interfaces = 0x03;
	for (i = 0; i < num_interfaces; i++) {
		g_debug ("claiming interface 0x%02x", i);
		if (!g_usb_device_claim_interface (priv->usb_device, i,
						   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
						   error)) {
			g_prefix_error (error, "Failed to claim 0x%02x: ", i);
			return FALSE;
		}
	}

	/* get config */
	if (priv->kind == UNIFYING_DONGLE_KIND_RUNTIME) {
		guint8 config[10];
		guint8 buf[15];
		guint8 cmd[] =  { UNIFYING_HIDPP_REPORT_ID_SHORT,
				  UNIFYING_HIDPP_DEVICE_INDEX_RECEIVER,
				  UNIFYING_HIDPP_GET_REGISTER_REQ,
				  UNIFYING_HIDPP_REGISTER_ADDR_VERSION,
				  0x00, 0x00, 0x00 };

		g_debug ("clearing existing data");
		if (!unifying_dongle_reset (dongle, error))
			return FALSE;

		/* read all 10 bytes of the version register */
		memset (config, 0x00, sizeof (config));
		for (i = 0; i < 0x05; i++) {
			cmd[4] = i;
			memset (buf, 0x00, sizeof (buf));
			if (!unifying_dongle_send_command (dongle, 0x0210, 0x0002,
							      cmd, sizeof (cmd),
							      buf, sizeof (buf),
							      UNIFYING_DONGLE_EP3,
							      error)) {
				g_prefix_error (error, "failed to read config 0x%02x: ", i);
				return FALSE;
			}
			memcpy (config + (i * 2), buf + 5, 2);
		}

		/* logitech sends base 16 and then pads as if base 10... */
		priv->version_firmware = g_strdup_printf ("%03x.%03x.%02x%03x",
							  config[2],
							  config[3],
							  config[4],
							  config[5]);
		priv->version_bootloader = g_strdup_printf ("BL.%03x.%03x",
							    config[8],
							    config[9]);
	} else {
		priv->version_firmware = g_strdup ("000.000.00000");
		priv->version_bootloader = g_strdup ("BL.000.000");
	}

	return TRUE;
}

gboolean
unifying_dongle_close (UnifyingDongle *dongle, GError **error)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	guint i;
	guint num_interfaces = 0x1;

	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (priv->usb_device == NULL)
		return TRUE;

	if (priv->kind == UNIFYING_DONGLE_KIND_RUNTIME)
		num_interfaces = 0x03;
	for (i = 0; i < num_interfaces; i++) {
		g_debug ("releasing interface 0x%02x", i);
		if (!g_usb_device_release_interface (priv->usb_device, i,
						     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
						     error)) {
			g_prefix_error (error, "Failed to release 0x%02x: ", i);
			return FALSE;
		}
	}

	g_debug ("closing device");
	if (!g_usb_device_close (priv->usb_device, error))
		return FALSE;
	return TRUE;
}

const gchar *
unifying_dongle_get_guid (UnifyingDongle *dongle)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	return priv->guid;
}

const gchar *
unifying_dongle_get_version_fw (UnifyingDongle *dongle)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	return priv->version_firmware;
}

const gchar *
unifying_dongle_get_version_bl (UnifyingDongle *dongle)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	return priv->version_bootloader;
}

static guint8
read_uint8 (const gchar *str)
{
	guint64 tmp;
	guint8 buf[3] = { 0x0, 0x0, 0x0 };
	memcpy (buf, str, 2);
	tmp = g_ascii_strtoull (buf, NULL, 16);
	return tmp;
}

typedef struct {
	guint8		 op;
	guint16		 addr;
	guint8		 data[32];
	gsize		 data_len;
} UnifyingDonglePayload;

static GPtrArray *
unifying_dongle_generate_payloads (GBytes *fw)
{
	GPtrArray *payloads;
	const gchar *tmp;
	g_auto(GStrv) lines = NULL;

	payloads = g_ptr_array_new_with_free_func (g_free);
	tmp = g_bytes_get_data (fw, NULL);
	lines = g_strsplit_set (tmp, "\n\r", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		UnifyingDonglePayload *payload;
		guint idx = 0x00;

		/* skip empty lines */
		tmp = lines[i];
		if (strlen (tmp) < 5)
			continue;

		payload = g_new0 (UnifyingDonglePayload, 1);
		payload->op = read_uint8 (tmp + 0x01);
		payload->addr = ((guint16) read_uint8 (tmp + 0x03)) << 8;
		payload->addr |= read_uint8 (tmp + 0x05);

		/* read the data, but skip the checksum byte */
		for (guint j = 0x09; tmp[j + 2] != '\0'; j += 2)
			payload->data[idx++] = read_uint8 (tmp + j);
		payload->data_len = idx;
		g_ptr_array_add (payloads, payload);
	}
	return payloads;
}

static gboolean
unifying_dongle_nordic_write_firmware (UnifyingDongle *dongle,
					GBytes *fw,
					GFileProgressCallback progress_cb,
					gpointer progress_data,
					GError **error)
{
	const UnifyingDonglePayload *payload;
	g_autoptr(GPtrArray) payloads = NULL;
	guint8 buf[32];

	/* init firmware transfer */
	memset (buf, 0x0, sizeof(buf));
	buf[0x00] = UNIFYING_BOOTLOADER_CMD_INIT_TRANSFER;
	if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
					      buf,
					      sizeof (buf),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to init fw transfer: ");
		return FALSE;
	}

	/* erase firmware pages up to the bootloader */
	memset (buf, 0x0, sizeof(buf));
	for (guint i = 0; i < UNIFYING_FIRMWARE_SIZE; i += 0x200) {
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_ERASE_PAGE;
		buf[0x01] = i << 8;
		buf[0x02] = 0x00;
		buf[0x03] = 0x01;
		if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
						      buf, sizeof (buf),
						      NULL, 0,
						      UNIFYING_DONGLE_EP1,
						      error)) {
			g_prefix_error (error, "failed to erase fw @0x%02x: ", i);
			return FALSE;
		}
	}

	/* transfer payload */
	payloads = unifying_dongle_generate_payloads (fw);
	for (guint i = 1; i < payloads->len; i++) {
		payload = g_ptr_array_index (payloads, i);

		/* skip the bootloader */
		if (payload->addr > UNIFYING_FIRMWARE_SIZE)
			break;

		/* build packet */
		memset (buf, 0x00, sizeof (buf));
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_PAYLOAD;
		buf[0x01] = payload->addr >> 8;
		buf[0x02] = payload->addr & 0xff;
		buf[0x03] = payload->op;
		memcpy (buf + 0x04, payload->data, payload->data_len);
		if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
						      buf, sizeof (buf),
						      NULL, 0,
						      UNIFYING_DONGLE_EP1,
						      error)) {
			g_prefix_error (error, "failed to transfer fw @0x%02x: ", i);
			return FALSE;
		}
		if (progress_cb != NULL) {
			progress_cb ((goffset) i * 32,
				     (goffset) payloads->len * 32,
				     progress_data);
		}
	}

	/* send the first managed packet last */
	payload = g_ptr_array_index (payloads, 0);
	memset (buf, 0x00, sizeof (buf));
	buf[0x00] = UNIFYING_BOOTLOADER_CMD_PAYLOAD;
	buf[0x01] = (payload->addr + 1) >> 8;
	buf[0x02] = (payload->addr + 1) & 0xff;
	buf[0x03] = payload->op - 1;
	memcpy (buf + 0x04, payload->data + 1, payload->data_len - 1);
	if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to transfer fw start: ");
		return FALSE;
	}

	/* mark as complete */
	if (progress_cb != NULL) {
		progress_cb ((goffset) payloads->len * 32,
			     (goffset) payloads->len * 32,
			     progress_data);
	}

	/* completed upload */
	memset (buf, 0x0, sizeof(buf));
	buf[0x00] = UNIFYING_BOOTLOADER_CMD_PAYLOAD;
	buf[0x01] = 0x00;
	buf[0x02] = 0x00;
	buf[0x03] = 0x01;
	buf[0x04] = 0x02;
	if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to set completed: ");
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static gboolean
unifying_dongle_texas_write_address (UnifyingDongle *dongle,
					guint16 addr,
					GError **error)
{
	guint8 buf[32];
	memset (buf, 0x00, sizeof (buf));
	if (addr == 0x0400) {
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_SET_ADDRESS;
		buf[0x01] = 0x00;
		buf[0x02] = 0x00;
		buf[0x03] = 0x01;
		buf[0x04] = 0x00;
	} else {
		guint16 addr_tmp = addr - 0x80;
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_SET_ADDRESS;
		buf[0x01] = addr_tmp >> 8;
		buf[0x02] = addr_tmp & 0xff;
		buf[0x03] = 0x01;
		buf[0x04] = 0x01;
	}
	if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to set address @0x%04x: ", addr);
		return FALSE;
	}
	memset (buf, 0x00, sizeof (buf));
	if (addr == 0x6c00) {
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_SET_ADDRESS;
		buf[0x01] = 0x00;
		buf[0x02] = 0x00;
		buf[0x03] = 0x01;
		buf[0x04] = 0x03;
	} else {
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_SET_ADDRESS;
		buf[0x01] = 0x00;
		buf[0x02] = 0x00;
		buf[0x03] = 0x01;
		buf[0x04] = 0x02;
	}
	if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to clear address @0x%04x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
unifying_dongle_texas_write_firmware (UnifyingDongle *dongle,
					 GBytes *fw,
					 GFileProgressCallback progress_cb,
					 gpointer progress_data,
					 GError **error)
{
	const UnifyingDonglePayload *payload;
	guint16 last_set_addr = 0xffff;
	guint8 buf[32];
	g_autoptr(GPtrArray) payloads = NULL;

	/* init firmware transfer */
	memset (buf, 0x0, sizeof(buf));
	buf[0x00] = UNIFYING_BOOTLOADER_CMD_INIT_TRANSFER;
	if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
					      buf,
					      sizeof (buf),
					      NULL, 0,
					      UNIFYING_DONGLE_EP1,
					      error)) {
		g_prefix_error (error, "failed to init fw transfer: ");
		return FALSE;
	}

	/* transfer payload */
	payloads = unifying_dongle_generate_payloads (fw);
	for (guint i = 0; i < payloads->len; i++) {
		payload = g_ptr_array_index (payloads, i);

		/* skip the bootloader */
		if (payload->addr >= UNIFYING_FIRMWARE_SIZE)
			break;

		/* skip the header */
		if (payload->addr < 0x0400)
			continue;

		/* skip record ??? */
		if (payload->op == 0x02)
			continue;

		/* set address */
		if (last_set_addr == 0xffff || payload->addr - last_set_addr >= 0x80) {
			if (!unifying_dongle_texas_write_address (dongle,
								     payload->addr,
								     error))
				return FALSE;
			last_set_addr = payload->addr;
		}

		/* build packet */
		memset (buf, 0x00, sizeof (buf));
		buf[0x00] = UNIFYING_BOOTLOADER_CMD_WRITE_PAGE;
		buf[0x01] = 0x00;
		buf[0x02] = payload->addr & 0x7f;
		buf[0x03] = payload->op;
		memcpy (buf + 0x04, payload->data, payload->data_len);
		if (!unifying_dongle_send_command (dongle, 0x0200, 0x0000,
						      buf, sizeof (buf),
						      NULL, 0,
						      UNIFYING_DONGLE_EP1,
						      error)) {
			g_prefix_error (error, "failed to transfer fw @0x%02x: ", i);
			return FALSE;
		}
		if (progress_cb != NULL) {
			progress_cb ((goffset) i * 32,
				     (goffset) payloads->len * 32,
				     progress_data);
		}
	}

	/* finish page */
	if (!unifying_dongle_texas_write_address (dongle,
						     last_set_addr + 0x80,
						     error))
		return FALSE;

	/* success! */
	return TRUE;
}

gboolean
unifying_dongle_write_firmware (UnifyingDongle *dongle,
				   GBytes *fw,
				   GFileProgressCallback progress_cb,
				   gpointer progress_data,
				   GError **error)
{
	UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);

	g_return_val_if_fail (UNIFYING_IS_DONGLE (dongle), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* corrupt */
	if (g_bytes_get_size (fw) < 0x4000) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware is too small");
		return FALSE;
	}

	/* nordic style */
	if (priv->kind == UNIFYING_DONGLE_KIND_BOOTLOADER_NORDIC) {
		return unifying_dongle_nordic_write_firmware (dongle,
								 fw,
								 progress_cb,
								 progress_data,
								 error);
	}

	/* texas style */
	if (priv->kind == UNIFYING_DONGLE_KIND_BOOTLOADER_TEXAS) {
		return unifying_dongle_texas_write_firmware (dongle,
								fw,
								progress_cb,
								progress_data,
								error);
	}

	/* eeek */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "bootloader is not supported");
	return FALSE;
}

typedef struct {
	guint16			 vid;
	guint16			 pid;
} UnifyingVidPid;

UnifyingDongle *
unifying_dongle_new (GUsbDevice *usb_device)
{
	struct {
		guint16			 vid;
		guint16			 pid;
		UnifyingDongleKind	 kind;
	} vid_pids[] = {
		{ 0x046d, 0xc52b, UNIFYING_DONGLE_KIND_RUNTIME},
		{ 0x046d, 0xaaaa, UNIFYING_DONGLE_KIND_BOOTLOADER_NORDIC},
		{ 0x046d, 0xaaac, UNIFYING_DONGLE_KIND_BOOTLOADER_TEXAS},
		{ 0x0000, 0x0000, 0 }
	};
	for (guint i = 0; vid_pids[i].vid != 0x0000; i++) {
		if (g_usb_device_get_vid (usb_device) == vid_pids[i].vid &&
		    g_usb_device_get_pid (usb_device) == vid_pids[i].pid) {
			UnifyingDongle *dongle = g_object_new (UNIFYING_TYPE_DONGLE, NULL);
			UnifyingDonglePrivate *priv = GET_PRIVATE (dongle);
			priv->usb_device = g_object_ref (usb_device);
			priv->kind = vid_pids[i].kind;
			return dongle;
		}
	}

	return NULL;
}

UnifyingDongle *
unifying_dongle_emulated_new (UnifyingDongleKind kind)
{
	UnifyingDongle *dongle;
	UnifyingDonglePrivate *priv;
	dongle = g_object_new (UNIFYING_TYPE_DONGLE, NULL);
	priv = GET_PRIVATE (dongle);
	priv->kind = kind;
	return dongle;
}
