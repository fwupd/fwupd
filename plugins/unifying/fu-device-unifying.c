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

#include "fu-device-unifying.h"

#define UNIFYING_REQUEST_SET_REPORT		0x09
#define FU_DEVICE_UNIFYING_TIMEOUT_MS		2500
#define FU_DEVICE_UNIFYING_EP1			0x81
#define FU_DEVICE_UNIFYING_EP3			0x83

/*
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */
#define HIDPP_RECEIVER_IDX			0xFF
#define HIDPP_WIRED_DEVICE_IDX			0x00

#define HIDPP_REPORT_ID_SHORT			0x10
#define HIDPP_REPORT_ID_LONG			0x11
#define HIDPP_REPORT_ID_MEDIUM			0x20

#define HIDPP_SHORT_MESSAGE_LENGTH		7
#define HIDPP_LONG_MESSAGE_LENGTH		20

#define HIDPP_SET_REGISTER_REQ			0x80
#define HIDPP_SET_REGISTER_RSP			0x80
#define HIDPP_GET_REGISTER_REQ			0x81
#define HIDPP_GET_REGISTER_RSP			0x81
#define HIDPP_SET_LONG_REGISTER_REQ		0x82
#define HIDPP_SET_LONG_REGISTER_RSP		0x82
#define HIDPP_GET_LONG_REGISTER_REQ		0x83
#define HIDPP_GET_LONG_REGISTER_RSP		0x83
#define HIDPP_ERROR_MSG				0x8F

#define HIDPP_ERR_SUCCESS			0x00
#define HIDPP_ERR_INVALID_SUBID			0x01
#define HIDPP_ERR_INVALID_ADDRESS		0x02
#define HIDPP_ERR_INVALID_VALUE			0x03
#define HIDPP_ERR_CONNECT_FAIL			0x04
#define HIDPP_ERR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERR_ALREADY_EXISTS		0x06
#define HIDPP_ERR_BUSY				0x07
#define HIDPP_ERR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERR_RESOURCE_ERROR		0x09
#define HIDPP_ERR_REQUEST_UNAVAILABLE		0x0A
#define HIDPP_ERR_INVALID_PARAM_VALUE		0x0B
#define HIDPP_ERR_WRONG_PIN_CODE		0x0C

/*
 * HID++ 1.0 registers
 */

#define HIDPP_REGISTER_HIDPP_NOTIFICATIONS			0x00
#define HIDPP_REGISTER_ENABLE_INDIVIDUAL_FEATURES		0x01
#define HIDPP_REGISTER_BATTERY_STATUS				0x07
#define HIDPP_REGISTER_BATTERY_MILEAGE				0x0D
#define HIDPP_REGISTER_PROFILE					0x0F
#define HIDPP_REGISTER_LED_STATUS				0x51
#define HIDPP_REGISTER_LED_INTENSITY				0x54
#define HIDPP_REGISTER_LED_COLOR				0x57
#define HIDPP_REGISTER_OPTICAL_SENSOR_SETTINGS			0x61
#define HIDPP_REGISTER_CURRENT_RESOLUTION			0x63
#define HIDPP_REGISTER_USB_REFRESH_RATE				0x64
#define HIDPP_REGISTER_GENERIC_MEMORY_MANAGEMENT		0xA0
#define HIDPP_REGISTER_HOT_CONTROL				0xA1
#define HIDPP_REGISTER_READ_MEMORY				0xA2
#define HIDPP_REGISTER_DEVICE_CONNECTION_DISCONNECTION		0xB2
#define HIDPP_REGISTER_PAIRING_INFORMATION			0xB5
#define HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE		0xF0
#define HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION		0xF1

/*
 * HID++ 2.0 pages
 */

#define HIDPP_PAGE_ROOT						0x0000
#define HIDPP_PAGE_FEATURE_SET					0x0001
#define HIDPP_PAGE_DEVICE_INFO					0x0003
#define HIDPP_PAGE_BATTERY_LEVEL_STATUS				0x1000
#define HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS			0x1b00
#define HIDPP_PAGE_SPECIAL_KEYS_BUTTONS				0x1b04
#define HIDPP_PAGE_MOUSE_POINTER_BASIC				0x2200
#define HIDPP_PAGE_ADJUSTABLE_DPI				0x2201
#define HIDPP_PAGE_ADJUSTABLE_REPORT_RATE			0x8060
#define HIDPP_PAGE_COLOR_LED_EFFECTS				0x8070
#define HIDPP_PAGE_ONBOARD_PROFILES				0x8100
#define HIDPP_PAGE_MOUSE_BUTTON_SPY				0x8110

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
	FuDeviceUnifyingKind	 kind;
	GUsbDevice		*usb_device;
} FuDeviceUnifyingPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuDeviceUnifying, fu_device_unifying, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_device_unifying_get_instance_private (o))

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

FuDeviceUnifyingKind
fu_device_unifying_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "runtime") == 0)
		return FU_DEVICE_UNIFYING_KIND_RUNTIME;
	if (g_strcmp0 (kind, "bootloader-nordic") == 0)
		return FU_DEVICE_UNIFYING_KIND_BOOTLOADER_NORDIC;
	if (g_strcmp0 (kind, "bootloader-texas") == 0)
		return FU_DEVICE_UNIFYING_KIND_BOOTLOADER_TEXAS;
	return FU_DEVICE_UNIFYING_KIND_UNKNOWN;
}

const gchar *
fu_device_unifying_kind_to_string (FuDeviceUnifyingKind kind)
{
	if (kind == FU_DEVICE_UNIFYING_KIND_RUNTIME)
		return "runtime";
	if (kind == FU_DEVICE_UNIFYING_KIND_BOOTLOADER_NORDIC)
		return "bootloader-nordic";
	if (kind == FU_DEVICE_UNIFYING_KIND_BOOTLOADER_TEXAS)
		return "bootloader-texas";
	return NULL;
}

static void
fu_device_unifying_finalize (GObject *object)
{
	FuDeviceUnifying *device = FU_DEVICE_UNIFYING (object);
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);

	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);

	G_OBJECT_CLASS (fu_device_unifying_parent_class)->finalize (object);
}

static void
fu_device_unifying_init (FuDeviceUnifying *device)
{
}

static void
fu_device_unifying_class_init (FuDeviceUnifyingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_unifying_finalize;
}

FuDeviceUnifyingKind
fu_device_unifying_get_kind (FuDeviceUnifying *device)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	return priv->kind;
}

GUsbDevice *
fu_device_unifying_get_usb_device (FuDeviceUnifying *device)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	return priv->usb_device;
}

static gboolean
fu_device_unifying_send_command (FuDeviceUnifying *device,
				 guint16 value,
				 guint16 idx,
				 const guint8 *data_in,
				 gsize data_in_length,
				 guint8 *data_out,
				 gsize data_out_length,
				 guint8 endpoint,
				 GError **error)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	gsize actual_length = 0;
	guint8 buf[32];

	/* send request */
	fu_unifying_dump_raw ("host->device", data_in, data_in_length);
	if (priv->usb_device != NULL) {
		g_autofree guint8 *data_in_buf = g_memdup (data_in, data_in_length);
		if (!g_usb_device_control_transfer (priv->usb_device,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    UNIFYING_REQUEST_SET_REPORT,
						    value, idx,
						    data_in_buf, data_in_length,
						    &actual_length,
						    FU_DEVICE_UNIFYING_TIMEOUT_MS,
						    NULL,
						    error)) {
			g_prefix_error (error, "failed to send data: ");
			return FALSE;
		}
	}

	/* get response */
	memset (buf, 0x00, sizeof (buf));
	if (priv->usb_device != NULL) {
		if (!g_usb_device_interrupt_transfer (priv->usb_device,
						      endpoint,
						      buf,
						      sizeof (buf),
						      &actual_length,
						      FU_DEVICE_UNIFYING_TIMEOUT_MS,
						      NULL,
						      error)) {
			g_prefix_error (error, "failed to get data: ");
			return FALSE;
		}
	} else {
		/* emulated */
		actual_length = data_out_length;
	}
	fu_unifying_dump_raw ("device->host", buf, actual_length);

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
fu_device_unifying_detach (FuDeviceUnifying *device, GError **error)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	guint8 cmd[] = { HIDPP_REPORT_ID_SHORT,
			 HIDPP_RECEIVER_IDX,
			 HIDPP_SET_REGISTER_REQ,
			 HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE,
			 0x49, 0x43, 0x50 /* value */};

	g_return_val_if_fail (FU_IS_DEVICE_UNIFYING (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (priv->kind != FU_DEVICE_UNIFYING_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "device is not in runtime state");
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
					    FU_DEVICE_UNIFYING_TIMEOUT_MS,
					    NULL,
					    error)) {
		g_prefix_error (error, "failed to detach to bootloader: ");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_device_unifying_attach (FuDeviceUnifying *device, GError **error)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	guint8 cmd[32];

	g_return_val_if_fail (FU_IS_DEVICE_UNIFYING (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "device is not in bootloader state");
		return FALSE;
	}

	/* attach */
	memset (cmd, 0x0, sizeof(cmd));
	cmd[0x0] = UNIFYING_BOOTLOADER_CMD_REBOOT;
	fu_unifying_dump_raw ("host->device", cmd, sizeof (cmd));
	if (!fu_device_unifying_send_command (device,
					      0x0200, 0x0000,
					      cmd, sizeof (cmd),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
					      error)) {
		g_prefix_error (error, "failed to attach back to runtime: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_device_unifying_reset (FuDeviceUnifying *device, GError **error)
{
	const guint8 cmd[] =  { HIDPP_REPORT_ID_SHORT,
				HIDPP_RECEIVER_IDX,
				HIDPP_GET_REGISTER_REQ,
				HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION,
				0x00, 0x00, 0x00 };
	if (!fu_device_unifying_send_command (device, 0x0210, 0x0002,
					      cmd, sizeof (cmd),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP3,
					      error)) {
		g_prefix_error (error, "failed to reset");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_device_unifying_open (FuDeviceUnifying *device, GError **error)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	guint i;
	guint num_interfaces = 0x1;
	g_autofree gchar *devid = NULL;

	g_return_val_if_fail (FU_IS_DEVICE_UNIFYING (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (priv->usb_device == NULL) {
		fu_device_set_version (FU_DEVICE (device), "001.002.00003");
		fu_device_set_version_bootloader (FU_DEVICE (device), "BL.004.005");
		return TRUE;
	}

	/* open device */
	g_debug ("opening unifying device");
	if (!g_usb_device_open (priv->usb_device, error))
		return FALSE;
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_RUNTIME)
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
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_RUNTIME) {
		guint8 config[10];
		guint8 buf[15];
		guint8 cmd[] =  { HIDPP_REPORT_ID_SHORT,
				  HIDPP_RECEIVER_IDX,
				  HIDPP_GET_REGISTER_REQ,
				  HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION,
				  0x00, 0x00, 0x00 };
		g_autofree gchar *version_fw = NULL;
		g_autofree gchar *version_bl = NULL;

		g_debug ("clearing existing data");
		if (!fu_device_unifying_reset (device, error))
			return FALSE;

		/* read all 10 bytes of the version register */
		memset (config, 0x00, sizeof (config));
		for (i = 0; i < 0x05; i++) {
			cmd[4] = i;
			memset (buf, 0x00, sizeof (buf));
			if (!fu_device_unifying_send_command (device, 0x0210, 0x0002,
							      cmd, sizeof (cmd),
							      buf, sizeof (buf),
							      FU_DEVICE_UNIFYING_EP3,
							      error)) {
				g_prefix_error (error, "failed to read config 0x%02x: ", i);
				return FALSE;
			}
			memcpy (config + (i * 2), buf + 5, 2);
		}

		/* logitech sends base 16 and then pads as if base 10... */
		version_fw = g_strdup_printf ("%03x.%03x.%02x%03x",
					      config[2],
					      config[3],
					      config[4],
					      config[5]);
		version_bl = g_strdup_printf ("BL.%03x.%03x",
					      config[8],
					      config[9]);
		fu_device_set_version (FU_DEVICE (device), version_fw);
		fu_device_set_version_bootloader (FU_DEVICE (device), version_bl);
	} else {
		fu_device_set_version (FU_DEVICE (device), "000.000.00000");
		fu_device_set_version_bootloader (FU_DEVICE (device), "BL.000.000");
	}

	return TRUE;
}

gboolean
fu_device_unifying_close (FuDeviceUnifying *device, GError **error)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	guint i;
	guint num_interfaces = 0x1;

	g_return_val_if_fail (FU_IS_DEVICE_UNIFYING (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (priv->usb_device == NULL)
		return TRUE;

	if (priv->kind == FU_DEVICE_UNIFYING_KIND_RUNTIME)
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

static guint8
read_uint8 (const gchar *str)
{
	guint64 tmp;
	gchar buf[3] = { 0x0, 0x0, 0x0 };
	memcpy (buf, str, 2);
	tmp = g_ascii_strtoull (buf, NULL, 16);
	return tmp;
}

typedef struct {
	guint8		 op;
	guint16		 addr;
	guint8		 data[32];
	gsize		 data_len;
} FuDeviceUnifyingPayload;

static GPtrArray *
fu_device_unifying_generate_payloads (GBytes *fw)
{
	GPtrArray *payloads;
	const gchar *tmp;
	g_auto(GStrv) lines = NULL;

	payloads = g_ptr_array_new_with_free_func (g_free);
	tmp = g_bytes_get_data (fw, NULL);
	lines = g_strsplit_set (tmp, "\n\r", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		FuDeviceUnifyingPayload *payload;
		guint idx = 0x00;

		/* skip empty lines */
		tmp = lines[i];
		if (strlen (tmp) < 5)
			continue;

		payload = g_new0 (FuDeviceUnifyingPayload, 1);
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
fu_device_unifying_nordic_write_firmware (FuDeviceUnifying *device,
					GBytes *fw,
					GFileProgressCallback progress_cb,
					gpointer progress_data,
					GError **error)
{
	const FuDeviceUnifyingPayload *payload;
	g_autoptr(GPtrArray) payloads = NULL;
	guint8 buf[32];

	/* init firmware transfer */
	memset (buf, 0x0, sizeof(buf));
	buf[0x00] = UNIFYING_BOOTLOADER_CMD_INIT_TRANSFER;
	if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
					      buf,
					      sizeof (buf),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
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
		if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
						      buf, sizeof (buf),
						      NULL, 0,
						      FU_DEVICE_UNIFYING_EP1,
						      error)) {
			g_prefix_error (error, "failed to erase fw @0x%02x: ", i);
			return FALSE;
		}
	}

	/* transfer payload */
	payloads = fu_device_unifying_generate_payloads (fw);
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
		if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
						      buf, sizeof (buf),
						      NULL, 0,
						      FU_DEVICE_UNIFYING_EP1,
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
	if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
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
	if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
					      error)) {
		g_prefix_error (error, "failed to set completed: ");
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static gboolean
fu_device_unifying_texas_write_address (FuDeviceUnifying *device,
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
	if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
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
	if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
					      buf, sizeof (buf),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
					      error)) {
		g_prefix_error (error, "failed to clear address @0x%04x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_device_unifying_texas_write_firmware (FuDeviceUnifying *device,
					 GBytes *fw,
					 GFileProgressCallback progress_cb,
					 gpointer progress_data,
					 GError **error)
{
	const FuDeviceUnifyingPayload *payload;
	guint16 last_set_addr = 0xffff;
	guint8 buf[32];
	g_autoptr(GPtrArray) payloads = NULL;

	/* init firmware transfer */
	memset (buf, 0x0, sizeof(buf));
	buf[0x00] = UNIFYING_BOOTLOADER_CMD_INIT_TRANSFER;
	if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
					      buf,
					      sizeof (buf),
					      NULL, 0,
					      FU_DEVICE_UNIFYING_EP1,
					      error)) {
		g_prefix_error (error, "failed to init fw transfer: ");
		return FALSE;
	}

	/* transfer payload */
	payloads = fu_device_unifying_generate_payloads (fw);
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
			if (!fu_device_unifying_texas_write_address (device,
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
		if (!fu_device_unifying_send_command (device, 0x0200, 0x0000,
						      buf, sizeof (buf),
						      NULL, 0,
						      FU_DEVICE_UNIFYING_EP1,
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
	if (!fu_device_unifying_texas_write_address (device,
						     last_set_addr + 0x80,
						     error))
		return FALSE;

	/* success! */
	return TRUE;
}

gboolean
fu_device_unifying_write_firmware (FuDeviceUnifying *device,
				   GBytes *fw,
				   GFileProgressCallback progress_cb,
				   gpointer progress_data,
				   GError **error)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);

	g_return_val_if_fail (FU_IS_DEVICE_UNIFYING (device), FALSE);
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
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_BOOTLOADER_NORDIC) {
		return fu_device_unifying_nordic_write_firmware (device,
								 fw,
								 progress_cb,
								 progress_data,
								 error);
	}

	/* texas style */
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_BOOTLOADER_TEXAS) {
		return fu_device_unifying_texas_write_firmware (device,
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

/* now with kind and usb_device set */
static void
fu_device_unifying_init_real (FuDeviceUnifying *device)
{
	FuDeviceUnifyingPrivate *priv = GET_PRIVATE (device);
	guint16 pid_for_guid = 0xffff;
	g_autofree gchar *devid = NULL;
	g_autofree gchar *name = NULL;

	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (device),
			    FWUPD_DEVICE_FLAG_ALLOW_ONLINE);

	/* set default vendor */
	fu_device_set_vendor (FU_DEVICE (device), "Logitech");

	/* generate name */
	name = g_strdup_printf ("Unifying [%s]",
				fu_device_unifying_kind_to_string (priv->kind));
	fu_device_set_name (FU_DEVICE (device), name);

	/* generate GUID -- in runtime mode we have to use the release */
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_RUNTIME) {
		guint16 release = g_usb_device_get_release (priv->usb_device);
		switch (release &= 0xff00) {
		case 0x1200:
			/* Nordic */
			pid_for_guid = 0xaaaa;
			break;
		case 0x2400:
			/* Texas */
			pid_for_guid = 0xaaac;
			break;
		default:
			g_warning ("bootloader release %04x invalid", release);
			break;
		}
	} else {
		pid_for_guid = g_usb_device_get_pid (priv->usb_device);
	}
	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				 g_usb_device_get_vid (priv->usb_device),
				 pid_for_guid);

	fu_device_add_guid (FU_DEVICE (device), devid);

	/* only the bootloader can do the update */
	if (priv->kind == FU_DEVICE_UNIFYING_KIND_RUNTIME) {
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}
}

FuDeviceUnifying *
fu_device_unifying_new (GUsbDevice *usb_device)
{
	FuDeviceUnifying *device;
	FuDeviceUnifyingPrivate *priv;
	struct {
		guint16			 vid;
		guint16			 pid;
		FuDeviceUnifyingKind	 kind;
	} vidpids[] = {
		{ 0x046d, 0xc52b, FU_DEVICE_UNIFYING_KIND_RUNTIME},
		{ 0x046d, 0xaaaa, FU_DEVICE_UNIFYING_KIND_BOOTLOADER_NORDIC},
		{ 0x046d, 0xaaac, FU_DEVICE_UNIFYING_KIND_BOOTLOADER_TEXAS},
		{ 0x0000, 0x0000, 0 }
	};
	for (guint i = 0; vidpids[i].vid != 0x0000; i++) {
		if (g_usb_device_get_vid (usb_device) != vidpids[i].vid)
			continue;
		if (g_usb_device_get_pid (usb_device) != vidpids[i].pid)
			continue;
		device = g_object_new (FU_TYPE_DEVICE_UNIFYING, NULL);
		priv = GET_PRIVATE (device);
		priv->kind = vidpids[i].kind;
		priv->usb_device = g_object_ref (usb_device);
		fu_device_unifying_init_real (device);
		return device;
	}
	return NULL;
}

FuDeviceUnifying *
fu_device_unifying_emulated_new (FuDeviceUnifyingKind kind)
{
	FuDeviceUnifying *device;
	FuDeviceUnifyingPrivate *priv;
	device = g_object_new (FU_TYPE_DEVICE_UNIFYING, NULL);
	priv = GET_PRIVATE (device);
	priv->kind = kind;
	return device;
}
