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
} LuDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LuDevice, lu_device, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_KIND,
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

LuDeviceHidppMsg *
lu_device_hidpp_new (void)
{
	return g_new0 (LuDeviceHidppMsg, 1);
}

static void
lu_device_hidpp_dump (LuDevice *device, const gchar *title, LuDeviceHidppMsg *msg)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_autofree gchar *title_prefixed = NULL;
	if (priv->usb_device != NULL)
		title_prefixed = g_strdup_printf ("[USB] %s", title);
	else if (priv->udev_device != NULL)
		title_prefixed = g_strdup_printf ("[HID] %s", title);
	else
		title_prefixed = g_strdup_printf ("[EMU] %s", title);
	lu_dump_raw (title_prefixed, (guint8 *) msg, msg->len + 3);
}

gboolean
lu_device_hidpp_send (LuDevice *device, LuDeviceHidppMsg *msg, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);

	lu_device_hidpp_dump (device, "host->device", msg);

	/* USB */
	if (priv->usb_device != NULL) {
		gsize actual_length = 0;
		if (!g_usb_device_control_transfer (priv->usb_device,
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_CLASS,
						    G_USB_DEVICE_RECIPIENT_INTERFACE,
						    LU_REQUEST_SET_REPORT,
						    0x0210, 0x0002,
						    (guint8 *) msg, msg->len + 3,
						    &actual_length,
						    LU_DEVICE_TIMEOUT_MS,
						    NULL,
						    error)) {
			g_prefix_error (error, "failed to send data: ");
			return FALSE;
		}
		if (actual_length != msg->len + 3) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to send data: "
				     "wrote %" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT,
				     actual_length, msg->len + 3);
			return FALSE;
		}

	/* HID */
	} else if (priv->udev_device != NULL) {
		gssize len;
		len = write (priv->udev_device_fd, (guint8 *) msg, msg->len + 3);
		if (len != (gssize) msg->len + 3) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to send data: "
				     "wrote %" G_GSSIZE_FORMAT " of %" G_GSIZE_FORMAT,
				     len, msg->len + 3);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
lu_device_hidpp_receive (LuDevice *device, LuDeviceHidppMsg *msg, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);

	/* USB */
	if (priv->usb_device != NULL) {
		gsize actual_length = 0;
		if (!g_usb_device_interrupt_transfer (priv->usb_device,
						      LU_DEVICE_EP3,
						      (guint8 *) msg,
						      sizeof (msg),
						      &actual_length,
						      LU_DEVICE_TIMEOUT_MS,
						      NULL,
						      error)) {
			g_prefix_error (error, "failed to get data: ");
			return FALSE;
		}
		msg->len = actual_length - 3;

	/* HID */
	} else if (priv->udev_device != NULL) {
		gssize len = 0;
		len = read (priv->udev_device_fd, (guint8 *) msg, sizeof (msg));
		if (len < 0) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "failed to read data");
			return FALSE;
		}
		msg->len = len - 3;
	}

	/* success */
	lu_device_hidpp_dump (device, "device->host", msg);
	return TRUE;
}

gboolean
lu_device_hidpp_transfer (LuDevice *device, LuDeviceHidppMsg *msg, GError **error)
{
	g_autoptr(LuDeviceHidppMsg) msg_tmp = lu_device_hidpp_new ();

	/* send */
	if (!lu_device_hidpp_send (device, msg, error))
		return FALSE;

	/* recieve */
	if (!lu_device_hidpp_receive (device, msg_tmp, error))
		return FALSE;

	/* check the response was valid */
	if (msg->report_id != msg_tmp->report_id) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid report_id response");
		return FALSE;
	}
	if (0&&msg->device_idx != msg_tmp->device_idx) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid device_idx response");
		return FALSE;
	}
	if (msg->sub_id == HIDPP_SET_REGISTER_REQ &&
	    msg_tmp->sub_id != HIDPP_SET_REGISTER_RSP) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid sub_id response");
		return FALSE;
	}

	/* ensure data length correct */
	if (0 && msg_tmp->len + 3 != HIDPP_SHORT_MESSAGE_LENGTH &&
	    msg_tmp->len + 3 != HIDPP_LONG_MESSAGE_LENGTH) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "invalid length response");
		return FALSE;
	}

	/* copy over data */
	memset (msg->data, 0x00, sizeof(msg->data));
	memcpy (msg->data, msg_tmp->data, msg_tmp->len);
	msg->len = msg_tmp->len = 0;
	return TRUE;
}

LuDeviceKind
lu_device_get_kind (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->kind;
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
lu_device_open (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);
	LuDevicePrivate *priv = GET_PRIVATE (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
		g_debug ("opening unifying device using HID");
		priv->udev_device_fd = g_open (devpath, O_RDWR, 0);
		if (priv->udev_device_fd < 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to open %s", devpath);
			return FALSE;
		}
	}

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (device, error))
			return FALSE;
	}

	/* subclassed */
	if (klass->probe != NULL) {
		if (!klass->probe (device, error))
			return FALSE;
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

	return TRUE;
}
gboolean
lu_device_detach (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (lu_device_get_kind (device) != LU_DEVICE_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "device is not in runtime state");
		return FALSE;
	}

	/* subclassed */
	g_debug ("detaching device");
	if (klass->detach != NULL)
		return klass->detach (device, error);

	return TRUE;
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

	/* call distro-specific method */
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
	case PROP_FLAGS:
		g_value_set_uint (value, priv->flags);
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
	case PROP_FLAGS:
		priv->flags = g_value_get_uint (value);
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
	priv->guids = g_ptr_array_new_with_free_func (g_free);
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

	pspec = g_param_spec_uint ("flags", NULL, NULL,
				   LU_DEVICE_FLAG_NONE,
				   LU_DEVICE_FLAG_LAST,
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
