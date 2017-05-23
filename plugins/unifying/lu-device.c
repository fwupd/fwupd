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

#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>

#include "lu-common.h"
#include "lu-device-bootloader-nordic.h"
#include "lu-device-bootloader-texas.h"
#include "lu-device.h"
#include "lu-device-runtime.h"
#include "lu-hidpp.h"

typedef struct
{
	LuDeviceKind		 kind;
	GUdevDevice		*udev_device;
	gint			 udev_device_fd;
	GUsbDevice		*usb_device;
	gchar			*platform_id;
	gchar			*product;
	gchar			*vendor;
	gchar			*version_bl;
	gchar			*version_fw;
	GPtrArray		*guids;
	LuDeviceFlags		 flags;
	guint8			 hidpp_id;
	guint8			 battery_level;
	guint8			 hidpp_version;
	GPtrArray		*feature_index;
} LuDevicePrivate;

typedef struct {
	guint8			 idx;
	guint16			 feature;
} LuDeviceHidppMap;

G_DEFINE_TYPE_WITH_PRIVATE (LuDevice, lu_device, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_KIND,
	PROP_HIDPP_ID,
	PROP_FLAGS,
	PROP_PLATFORM_ID,
	PROP_UDEV_DEVICE,
	PROP_USB_DEVICE,
	PROP_LAST
};

#define GET_PRIVATE(o) (lu_device_get_instance_private (o))

LuDeviceKind
lu_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "runtime") == 0)
		return LU_DEVICE_KIND_RUNTIME;
	if (g_strcmp0 (kind, "bootloader-nordic") == 0)
		return LU_DEVICE_KIND_BOOTLOADER_NORDIC;
	if (g_strcmp0 (kind, "bootloader-texas") == 0)
		return LU_DEVICE_KIND_BOOTLOADER_TEXAS;
	if (g_strcmp0 (kind, "peripheral") == 0)
		return LU_DEVICE_KIND_PERIPHERAL;
	return LU_DEVICE_KIND_UNKNOWN;
}

const gchar *
lu_device_kind_to_string (LuDeviceKind kind)
{
	if (kind == LU_DEVICE_KIND_RUNTIME)
		return "runtime";
	if (kind == LU_DEVICE_KIND_BOOTLOADER_NORDIC)
		return "bootloader-nordic";
	if (kind == LU_DEVICE_KIND_BOOTLOADER_TEXAS)
		return "bootloader-texas";
	if (kind == LU_DEVICE_KIND_PERIPHERAL)
		return "peripheral";
	return NULL;
}

static const gchar *
lu_hidpp_feature_to_string (guint feature)
{
	if (feature == HIDPP_FEATURE_ROOT)
		return "Root";
	if (feature == HIDPP_FEATURE_I_FIRMWARE_INFO)
		return "IFirmwareInfo";
	if (feature == HIDPP_FEATURE_BATTERY_LEVEL_STATUS)
		return "BatteryLevelStatus";
	if (feature == HIDPP_FEATURE_DFU_CONTROL)
		return "DfuControl";
	if (feature == HIDPP_FEATURE_DFU_CONTROL_SIGNED)
		return "DfuControlSigned";
	if (feature == HIDPP_FEATURE_DFU)
		return "Dfu";
	return NULL;
}

LuDeviceHidppMsg *
lu_device_hidpp_new (void)
{
	return g_new0 (LuDeviceHidppMsg, 1);
}

#define HIDPP_REPORT_NOTIFICATION	0x01
#define HIDPP_REPORT_02			0x02
#define HIDPP_REPORT_03			0x03
#define HIDPP_REPORT_04			0x04
#define HIDPP_REPORT_20			0x20

static void
hidpp_device_map_print (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		LuDeviceHidppMap *map = g_ptr_array_index (priv->feature_index, i);
		g_debug ("%02x\t[%04x] %s",
			 map->idx,
			 map->feature,
			 lu_hidpp_feature_to_string (map->feature));
	}
}

guint8
lu_device_hidpp_feature_get_idx (LuDevice *device, guint16 feature)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		LuDeviceHidppMap *map = g_ptr_array_index (priv->feature_index, i);
		if (map->feature == feature)
			return map->idx;
	}
	return 0x00;
}

static guint16
lu_device_hidpp_feature_find_by_idx (LuDevice *device, guint8 idx)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		LuDeviceHidppMap *map = g_ptr_array_index (priv->feature_index, i);
		if (map->idx == idx)
			return map->feature;
	}
	return 0x0000;
}

static gsize
lu_device_hidpp_msg_length (LuDeviceHidppMsg *msg)
{
	if (msg->report_id == HIDPP_REPORT_ID_SHORT)
		return 0x07;
	if (msg->report_id == HIDPP_REPORT_ID_LONG)
		return 0x14;
	if (msg->report_id == HIDPP_REPORT_NOTIFICATION)
		return 0x08;
	if (msg->report_id == HIDPP_REPORT_02)
		return 0x08;
	if (msg->report_id == HIDPP_REPORT_03)
		return 0x05;
	if (msg->report_id == HIDPP_REPORT_04)
		return 0x02;
	if (msg->report_id == HIDPP_REPORT_20)
		return 0x0f;
	g_warning ("report 0x%02x unknown length", msg->report_id);
	return 0x08;
}

static void
lu_device_hidpp_dump (LuDevice *device, const gchar *title, const guint8 *data, gsize len)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_autofree gchar *title_prefixed = NULL;
	if (priv->usb_device != NULL)
		title_prefixed = g_strdup_printf ("[USB] %s", title);
	else if (priv->udev_device != NULL)
		title_prefixed = g_strdup_printf ("[HID] %s", title);
	else
		title_prefixed = g_strdup_printf ("[EMU] %s", title);
	lu_dump_raw (title_prefixed, data, len);
}

gboolean
lu_device_hidpp_send (LuDevice *device,
		      LuDeviceHidppMsg *msg,
		      guint timeout,
		      GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	gsize len = lu_device_hidpp_msg_length (msg);

	lu_device_hidpp_dump (device, "host->device", (guint8 *) msg, len);

	/* USB */
	if (priv->usb_device != NULL) {
		gsize actual_length = 0;
		if (!g_usb_device_control_transfer (priv->usb_device,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    LU_REQUEST_SET_REPORT,
						    0x0210, 0x0002,
						    (guint8 *) msg, len,
						    &actual_length,
						    timeout,
						    NULL,
						    error)) {
			g_prefix_error (error, "failed to send data: ");
			return FALSE;
		}
		if (actual_length != len) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to send data: "
				     "wrote %" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT,
				     actual_length, len);
			return FALSE;
		}

	/* HID */
	} else if (priv->udev_device != NULL) {
		if (!lu_nonblock_write (priv->udev_device_fd,
					(guint8 *) msg, len, error)) {
			g_prefix_error (error, "failed to send: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
lu_device_hidpp_receive (LuDevice *device,
			 LuDeviceHidppMsg *msg,
			 guint timeout,
			 GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	gsize read_size = 0;

	/* USB */
	if (priv->usb_device != NULL) {
		if (!g_usb_device_interrupt_transfer (priv->usb_device,
						      LU_DEVICE_EP3,
						      (guint8 *) msg,
						      sizeof(LuDeviceHidppMsg),
						      &read_size,
						      timeout,
						      NULL,
						      error)) {
			g_prefix_error (error, "failed to get data: ");
			return FALSE;
		}

	/* HID */
	} else if (priv->udev_device != NULL) {
		if (!lu_nonblock_read (priv->udev_device_fd,
				       (guint8 *) msg,
				       sizeof(LuDeviceHidppMsg),
				       &read_size,
				       timeout,
				       error)) {
			g_prefix_error (error, "failed to receive: ");
			return FALSE;
		}
	}

	/* check long enough, but allow returning oversize packets */
	lu_device_hidpp_dump (device, "device->host", (guint8 *) msg, read_size);
	if (read_size < lu_device_hidpp_msg_length (msg)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "message length too small, "
			     "got %" G_GSIZE_FORMAT " expected %" G_GSIZE_FORMAT,
			     read_size, lu_device_hidpp_msg_length (msg));
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
lu_device_hidpp_transfer (LuDevice *device, LuDeviceHidppMsg *msg, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	const guint timeout = LU_DEVICE_TIMEOUT_MS;
	g_autoptr(LuDeviceHidppMsg) msg_tmp = lu_device_hidpp_new ();

	/* send */
	if (!lu_device_hidpp_send (device, msg, timeout, error))
		return FALSE;

	/* keep trying to receive until we get a valid reply */
	while (1) {
		if (!lu_device_hidpp_receive (device, msg_tmp, timeout, error))
			return FALSE;
		if (msg_tmp->report_id == 0x10 || msg_tmp->report_id == 0x11)
			break;
		g_debug ("ignoring message with report 0x%02x", msg_tmp->report_id);
	};

	/* if the HID++ ID is unset, grab it from the reply */
	if (priv->hidpp_id == HIDPP_DEVICE_ID_UNSET &&
	    msg_tmp->device_id != HIDPP_DEVICE_ID_UNSET) {
		priv->hidpp_id = msg_tmp->device_id;
		g_debug ("HID++ ID now %02x", priv->hidpp_id);
	}

	/* HID++ 1.0 error */
	if (msg_tmp->sub_id == HIDPP_SUBID_ERROR_MSG) {
		const gchar *tmp;
		guint16 feature;
		switch (msg_tmp->data[1]) {
		case HIDPP_ERR_INVALID_SUBID:
			feature = lu_device_hidpp_feature_find_by_idx (device, msg_tmp->sub_id);
			tmp = lu_hidpp_feature_to_string (feature);
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "invalid SubID %s [0x%02x] or command",
				     tmp, msg->sub_id);
			break;
		case HIDPP_ERR_INVALID_ADDRESS:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "invalid address");
			break;
		case HIDPP_ERR_INVALID_VALUE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "invalid value");
			break;
		case HIDPP_ERR_CONNECT_FAIL:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "connection request failed");
			break;
		case HIDPP_ERR_TOO_MANY_DEVICES:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NO_SPACE,
					     "too many devices connected");
			break;
		case HIDPP_ERR_ALREADY_EXISTS:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_EXISTS,
					     "already exists");
			break;
		case HIDPP_ERR_BUSY:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_BUSY,
					     "busy");
			break;
		case HIDPP_ERR_UNKNOWN_DEVICE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "unknown device");
			break;
		case HIDPP_ERR_RESOURCE_ERROR:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_HOST_UNREACHABLE,
					     "resource error");
			break;
		case HIDPP_ERR_REQUEST_UNAVAILABLE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_EXISTS,
					     "request not valid in current context");
			break;
		case HIDPP_ERR_INVALID_PARAM_VALUE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "request parameter has unsupported value");
			break;
		case HIDPP_ERR_WRONG_PIN_CODE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_CONNECTION_REFUSED,
					     "the pin code was wrong");
			break;
		default:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "generic failure");
			break;
		}
		return FALSE;
	}

	/* check the response was valid */
	if (0&&msg->report_id != msg_tmp->report_id) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid report_id response");
		return FALSE;
	}
	if (0&&msg->device_id != msg_tmp->device_id) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid device_id response");
		return FALSE;
	}
	if (msg->sub_id == HIDPP_SUBID_SET_REGISTER &&
	    msg_tmp->sub_id != HIDPP_SUBID_SET_REGISTER) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid sub_id response");
		return FALSE;
	}

	/* copy over data */
	memset (msg->data, 0x00, sizeof(msg->data));
	msg->device_id = msg_tmp->device_id;
	msg->sub_id = msg_tmp->sub_id;
	msg->function_id = msg_tmp->function_id;
	memcpy (msg->data, msg_tmp->data, sizeof(msg->data));

	return TRUE;
}

gboolean
lu_device_hidpp_feature_search (LuDevice *device, guint16 feature, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	LuDeviceHidppMap *map;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

	/* find the idx for the feature */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->hidpp_id;
	msg->sub_id = 0x00; /* rootIndex */
	msg->function_id = 0x00 << 4; /* getFeature */
	msg->data[0] = feature >> 8;
	msg->data[1] = feature;
	msg->data[2] = 0x00;
	if (!lu_device_hidpp_transfer (device, msg, error)) {
		g_prefix_error (error,
				"failed to get idx for feature %s [0x%04x]: ",
				lu_hidpp_feature_to_string (feature), feature);
		return FALSE;
	}

	/* zero index */
	if (msg->data[0] == 0x00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "feature %s [0x%04x] not found",
			     lu_hidpp_feature_to_string (feature), feature);
		return FALSE;
	}

	/* add to map */
	map = g_new0 (LuDeviceHidppMap, 1);
	map->idx = msg->data[0];
	map->feature = feature;
	g_ptr_array_add (priv->feature_index, map);
	g_debug ("added feature %s [0x%04x] as idx %02x",
		 lu_hidpp_feature_to_string (feature), feature, map->idx);
	return TRUE;
}

LuDeviceKind
lu_device_get_kind (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->kind;
}

guint8
lu_device_get_hidpp_id (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->hidpp_id;
}

void
lu_device_set_hidpp_id (LuDevice *device, guint8 hidpp_id)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->hidpp_id = hidpp_id;
}

guint8
lu_device_get_battery_level (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->battery_level;
}

void
lu_device_set_battery_level (LuDevice *device, guint8 percentage)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->battery_level = percentage;
}

guint8
lu_device_get_hidpp_version (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->hidpp_version;
}

void
lu_device_set_hidpp_version (LuDevice *device, guint8 hidpp_version)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->hidpp_version = hidpp_version;
}

const gchar *
lu_device_get_platform_id (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->platform_id;
}

void
lu_device_set_platform_id (LuDevice *device, const gchar *platform_id)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_free (priv->platform_id);
	priv->platform_id = g_strdup (platform_id);
}

const gchar *
lu_device_get_product (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->product;
}

void
lu_device_set_product (LuDevice *device, const gchar *product)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_free (priv->product);
	priv->product = g_strdup (product);
}

const gchar *
lu_device_get_vendor (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->vendor;
}

void
lu_device_set_vendor (LuDevice *device, const gchar *vendor)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_free (priv->vendor);
	priv->vendor = g_strdup (vendor);
}

const gchar *
lu_device_get_version_bl (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->version_bl;
}

void
lu_device_set_version_bl (LuDevice *device, const gchar *version_bl)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_free (priv->version_bl);
	priv->version_bl = g_strdup (version_bl);
}

const gchar *
lu_device_get_version_fw (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->version_fw;
}

void
lu_device_set_version_fw (LuDevice *device, const gchar *version_fw)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_free (priv->version_fw);
	priv->version_fw = g_strdup (version_fw);
}

GPtrArray *
lu_device_get_guids (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->guids;
}

void
lu_device_add_guid (LuDevice *device, const gchar *guid)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_ptr_array_add (priv->guids, g_strdup (guid));
}

gboolean
lu_device_has_flag (LuDevice *device, LuDeviceFlags flag)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return (priv->flags & flag) > 0;
}

void
lu_device_add_flag (LuDevice *device, LuDeviceFlags flag)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->flags |= flag;
	g_object_notify (G_OBJECT (device), "flags");
}

void
lu_device_remove_flag (LuDevice *device, LuDeviceFlags flag)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->flags &= ~flag;
	g_object_notify (G_OBJECT (device), "flags");
}

LuDeviceFlags
lu_device_get_flags (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->flags;
}

GUdevDevice *
lu_device_get_udev_device (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->udev_device;
}

GUsbDevice *
lu_device_get_usb_device (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->usb_device;
}

gboolean
lu_device_probe (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);
	if (klass->probe != NULL)
		return klass->probe (device, error);
	return TRUE;
}

gboolean
lu_device_open (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);
	LuDevicePrivate *priv = GET_PRIVATE (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (lu_device_has_flag (device, LU_DEVICE_FLAG_IS_OPEN))
		return TRUE;

	/* set default vendor */
	lu_device_set_vendor (device, "Logitech");

	/* open device */
	if (priv->usb_device != NULL) {
		g_debug ("opening unifying device using USB");
		if (!g_usb_device_open (priv->usb_device, error))
			return FALSE;
	}

	/* USB */
	if (priv->usb_device != NULL) {
		g_autofree gchar *devid = NULL;
		guint8 num_interfaces = 0x01;
		if (priv->kind == LU_DEVICE_KIND_RUNTIME)
			num_interfaces = 0x03;
		for (guint i = 0; i < num_interfaces; i++) {
			g_debug ("claiming interface 0x%02x", i);
			if (!g_usb_device_claim_interface (priv->usb_device, i,
							   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
							   error)) {
				g_prefix_error (error, "Failed to claim 0x%02x: ", i);
				g_usb_device_close (priv->usb_device, NULL);
				return FALSE;
			}
		}

		/* generate GUID */
		devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
					 g_usb_device_get_vid (priv->usb_device),
					 g_usb_device_get_pid (priv->usb_device));
		lu_device_add_guid (device, devid);

	/* HID */
	} else if (priv->udev_device != NULL) {
		const gchar *devpath = g_udev_device_get_device_file (priv->udev_device);
		g_debug ("opening unifying device using %s", devpath);
		priv->udev_device_fd = lu_nonblock_open (devpath, error);
		if (priv->udev_device_fd < 0)
			return FALSE;
	}

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (device, error)) {
			lu_device_close (device, NULL);
			return FALSE;
		}
	}
	lu_device_add_flag (device, LU_DEVICE_FLAG_IS_OPEN);

	/* subclassed */
	if (!lu_device_probe (device, error)) {
		lu_device_close (device, NULL);
		return FALSE;
	}

	/* show the HID++2.0 features we found */
	hidpp_device_map_print (device);

	/* success */
	return TRUE;
}

gboolean
lu_device_poll (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

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

	/* unifying receiver notification */
	if (msg->report_id == HIDPP_REPORT_ID_SHORT) {
		switch (msg->sub_id) {
		case HIDPP_SUBID_DEVICE_CONNECTION:
		case HIDPP_SUBID_DEVICE_DISCONNECTION:
		case HIDPP_SUBID_DEVICE_LOCKING_CHANGED:
			g_debug ("device changed");
			if (klass->poll != NULL)
				return klass->poll (device, error);
		default:
			break;
		}
	}
	return TRUE;
}

gboolean
lu_device_close (LuDevice *device, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	g_debug ("closing device");
	if (klass->close != NULL) {
		if (!klass->close (device, error))
			return FALSE;
	}

	/* USB */
	if (priv->usb_device != NULL) {
		guint8 num_interfaces = 0x01;
		if (priv->kind == LU_DEVICE_KIND_RUNTIME)
			num_interfaces = 0x03;
		for (guint i = 0; i < num_interfaces; i++) {
			g_autoptr(GError) error_local = NULL;
			g_debug ("releasing interface 0x%02x", i);
			if (!g_usb_device_release_interface (priv->usb_device, i,
							     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
							     &error_local)) {
				if (!g_error_matches (error_local,
						      G_USB_DEVICE_ERROR,
						      G_USB_DEVICE_ERROR_INTERNAL)) {
					g_set_error (error,
						     G_IO_ERROR,
						     G_IO_ERROR_FAILED,
						     "Failed to release 0x%02x: %s",
						     i, error_local->message);
					return FALSE;
				}
			}
		}
		if (!g_usb_device_close (priv->usb_device, error))
			return FALSE;
	}

	/* HID */
	if (priv->udev_device != NULL) {
		if (!g_close (priv->udev_device_fd, error))
			return FALSE;
	}

	/* success */
	lu_device_remove_flag (device, LU_DEVICE_FLAG_IS_OPEN);
	return TRUE;
}
gboolean
lu_device_detach (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	g_debug ("detaching device");
	if (klass->detach != NULL)
		return klass->detach (device, error);

	/* nothing to do */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "device detach is not supported");
	return FALSE;
}

gboolean
lu_device_attach (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (lu_device_get_kind (device) == LU_DEVICE_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "device is not in bootloader state");
		return FALSE;
	}

	/* subclassed */
	if (klass->attach != NULL)
		return klass->attach (device, error);

	return TRUE;
}

gboolean
lu_device_write_firmware (LuDevice *device,
			  GBytes *fw,
			  GFileProgressCallback progress_cb,
			  gpointer progress_data,
			  GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* corrupt */
	if (g_bytes_get_size (fw) < 0x4000) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware is too small");
		return FALSE;
	}

	/* call device-specific method */
	if (klass->write_firmware == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "not supported in %s",
			     lu_device_kind_to_string (lu_device_get_kind (device)));
		return FALSE;
	}

	/* call either nordic or texas vfunc */
	return klass->write_firmware (device, fw, progress_cb, progress_data, error);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)

static GUdevDevice *
lu_device_find_udev_device (GUsbDevice *usb_device)
{
	g_autoptr(GUdevClient) gudev_client = g_udev_client_new (NULL);
	g_autoptr(GList) devices = NULL;

	devices = g_udev_client_query_by_subsystem (gudev_client, "usb");
	for (GList *l = devices; l != NULL; l = l->next) {
		guint busnum;
		guint devnum;
		g_autoptr(GUdevDevice) udev_device = G_UDEV_DEVICE (l->data);
		g_autoptr(GUdevDevice) udev_parent = g_udev_device_get_parent (udev_device);

		busnum = g_udev_device_get_sysfs_attr_as_int (udev_parent, "busnum");
		if (busnum != g_usb_device_get_bus (usb_device))
			continue;
		devnum = g_udev_device_get_sysfs_attr_as_int (udev_parent, "devnum");
		if (devnum != g_usb_device_get_address (usb_device))
			continue;

		return g_object_ref (udev_parent);
	}
	return FALSE;
}

static void
lu_device_update_platform_id (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	if (priv->usb_device != NULL && priv->udev_device == NULL) {
		g_autoptr(GUdevDevice) udev_device = NULL;
		udev_device = lu_device_find_udev_device (priv->usb_device);
		if (udev_device != NULL) {
			const gchar *tmp = g_udev_device_get_sysfs_path (udev_device);
			lu_device_set_platform_id (device, tmp);
		}
	}
}

static void
lu_device_get_property (GObject *object, guint prop_id,
			GValue *value, GParamSpec *pspec)
{
	LuDevice *device = LU_DEVICE (object);
	LuDevicePrivate *priv = GET_PRIVATE (device);
	switch (prop_id) {
	case PROP_KIND:
		g_value_set_uint (value, priv->kind);
		break;
	case PROP_HIDPP_ID:
		g_value_set_uint (value, priv->hidpp_id);
		break;
	case PROP_FLAGS:
		g_value_set_uint64 (value, priv->flags);
		break;
	case PROP_PLATFORM_ID:
		g_value_set_string (value, priv->platform_id);
		break;
	case PROP_UDEV_DEVICE:
		g_value_set_object (value, priv->udev_device);
		break;
	case PROP_USB_DEVICE:
		g_value_set_object (value, priv->usb_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lu_device_set_property (GObject *object, guint prop_id,
			const GValue *value, GParamSpec *pspec)
{
	LuDevice *device = LU_DEVICE (object);
	LuDevicePrivate *priv = GET_PRIVATE (device);
	switch (prop_id) {
	case PROP_KIND:
		priv->kind = g_value_get_uint (value);
		break;
	case PROP_HIDPP_ID:
		priv->hidpp_id = g_value_get_uint (value);
		break;
	case PROP_FLAGS:
		priv->flags = g_value_get_uint64 (value);
		break;
	case PROP_PLATFORM_ID:
		g_free (priv->platform_id);
		priv->platform_id = g_value_dup_string (value);
		break;
	case PROP_UDEV_DEVICE:
		priv->udev_device = g_value_dup_object (value);
		break;
	case PROP_USB_DEVICE:
		priv->usb_device = g_value_dup_object (value);
		lu_device_update_platform_id (device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lu_device_finalize (GObject *object)
{
	LuDevice *device = LU_DEVICE (object);
	LuDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);
	if (priv->udev_device != NULL)
		g_object_unref (priv->udev_device);
	g_ptr_array_unref (priv->guids);
	g_ptr_array_unref (priv->feature_index);
	g_free (priv->platform_id);
	g_free (priv->product);
	g_free (priv->vendor);
	g_free (priv->version_fw);
	g_free (priv->version_bl);

	G_OBJECT_CLASS (lu_device_parent_class)->finalize (object);
}

static void
lu_device_init (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	LuDeviceHidppMap *map;

	priv->hidpp_id = HIDPP_DEVICE_ID_UNSET;
	priv->guids = g_ptr_array_new_with_free_func (g_free);
	priv->feature_index = g_ptr_array_new_with_free_func (g_free);

	/* add known root */
	map = g_new0 (LuDeviceHidppMap, 1);
	map->idx = 0x00;
	map->feature = HIDPP_FEATURE_ROOT;
	g_ptr_array_add (priv->feature_index, map);
}

static void
lu_device_class_init (LuDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = lu_device_finalize;
	object_class->get_property = lu_device_get_property;
	object_class->set_property = lu_device_set_property;

	pspec = g_param_spec_uint ("kind", NULL, NULL,
				   LU_DEVICE_KIND_UNKNOWN,
				   LU_DEVICE_KIND_LAST,
				   LU_DEVICE_KIND_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	pspec = g_param_spec_uint ("hidpp-id", NULL, NULL,
				   HIDPP_DEVICE_ID_WIRED,
				   HIDPP_DEVICE_ID_RECEIVER,
				   HIDPP_DEVICE_ID_UNSET,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_HIDPP_ID, pspec);

	pspec = g_param_spec_uint64 ("flags", NULL, NULL,
				     LU_DEVICE_FLAG_NONE,
				     0xffff,
				     LU_DEVICE_FLAG_NONE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_FLAGS, pspec);

	pspec = g_param_spec_string ("platform-id", NULL, NULL, NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_PLATFORM_ID, pspec);

	pspec = g_param_spec_object ("udev-device", NULL, NULL,
				     G_UDEV_TYPE_DEVICE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_UDEV_DEVICE, pspec);

	pspec = g_param_spec_object ("usb-device", NULL, NULL,
				     G_USB_TYPE_DEVICE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_USB_DEVICE, pspec);
}

LuDevice *
lu_device_fake_new (LuDeviceKind kind)
{
	LuDevice *device = NULL;
	switch (kind) {
	case LU_DEVICE_KIND_BOOTLOADER_NORDIC:
		device = g_object_new (LU_TYPE_DEVICE_BOOTLOADER_NORDIC,
				       "kind", kind,
				       NULL);
		break;
	case LU_DEVICE_KIND_BOOTLOADER_TEXAS:
		device = g_object_new (LU_TYPE_DEVICE_BOOTLOADER_TEXAS,
				       "kind", kind,
				       NULL);
		break;
	case LU_DEVICE_KIND_RUNTIME:
		device = g_object_new (LU_TYPE_DEVICE_RUNTIME,
				       "kind", kind,
				       NULL);
		break;
	default:
		break;
	}
	return device;
}
