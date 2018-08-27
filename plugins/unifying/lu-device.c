/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <appstream-glib.h>

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
	FuDeviceLocker		*usb_device_locker;
	gchar			*version_hw;
	LuDeviceFlags		 flags;
	guint8			 hidpp_id;
	guint8			 battery_level;
	gdouble			 hidpp_version;
	GPtrArray		*feature_index;
} LuDevicePrivate;

typedef struct {
	guint8			 idx;
	guint16			 feature;
} LuDeviceHidppMap;

G_DEFINE_TYPE_WITH_PRIVATE (LuDevice, lu_device, FU_TYPE_DEVICE)

enum {
	PROP_0,
	PROP_KIND,
	PROP_HIDPP_ID,
	PROP_FLAGS,
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
lu_hidpp_feature_to_string (guint16 feature)
{
	if (feature == HIDPP_FEATURE_ROOT)
		return "Root";
	if (feature == HIDPP_FEATURE_I_FIRMWARE_INFO)
		return "IFirmwareInfo";
	if (feature == HIDPP_FEATURE_GET_DEVICE_NAME_TYPE)
		return "GetDevicenameType";
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

static gchar *
lu_device_flags_to_string (LuDeviceFlags flags)
{
	GString *str = g_string_new (NULL);
	if (flags & LU_DEVICE_FLAG_REQUIRES_SIGNED_FIRMWARE)
		g_string_append (str, "signed-firmware,");
	if (flags & LU_DEVICE_FLAG_REQUIRES_RESET)
		g_string_append (str, "requires-reset,");
	if (flags & LU_DEVICE_FLAG_ACTIVE)
		g_string_append (str, "active,");
	if (flags & LU_DEVICE_FLAG_IS_OPEN)
		g_string_append (str, "is-open,");
	if (flags & LU_DEVICE_FLAG_REQUIRES_ATTACH)
		g_string_append (str, "requires-attach,");
	if (flags & LU_DEVICE_FLAG_REQUIRES_DETACH)
		g_string_append (str, "requires-detach,");
	if (flags & LU_DEVICE_FLAG_DETACH_WILL_REPLUG)
		g_string_append (str, "detach-will-replug,");
	if (str->len == 0) {
		g_string_append (str, "none");
	} else {
		g_string_truncate (str, str->len - 1);
	}
	return g_string_free (str, FALSE);
}

static void
lu_device_to_string (FuDevice *device, GString *str)
{
	LuDevice *self = LU_DEVICE (device);
	LuDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *flags_str = NULL;

	g_string_append_printf (str, "  Type:\t\t\t%s\n", lu_device_kind_to_string (priv->kind));
	flags_str = lu_device_flags_to_string (priv->flags);
	g_string_append_printf (str, "  Flags:\t\t%s\n", flags_str);
	g_string_append_printf (str, "  HidppVersion:\t\t%.2f\n", priv->hidpp_version);
	if (priv->hidpp_id != HIDPP_DEVICE_ID_UNSET)
		g_string_append_printf (str, "  HidppId:\t\t0x%02x\n", (guint) priv->hidpp_id);
	if (priv->udev_device_fd > 0)
		g_string_append_printf (str, "  UdevDevice:\t\t%i\n", priv->udev_device_fd);
	if (priv->usb_device != NULL)
		g_string_append_printf (str, "  UsbDevice:\t\t%p\n", priv->usb_device);
	if (priv->version_hw != NULL)
		g_string_append_printf (str, "  VersionHardware:\t%s\n", priv->version_hw);
	if (priv->battery_level != 0)
		g_string_append_printf (str, "  Battery-level:\t\t%u\n", priv->battery_level);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		LuDeviceHidppMap *map = g_ptr_array_index (priv->feature_index, i);
		g_string_append_printf (str, "  Feature%02x:\t\t%s [0x%04x]\n",
					map->idx,
					lu_hidpp_feature_to_string (map->feature),
					map->feature);
	}

	/* fixme: superclass? */
	if (LU_IS_DEVICE_BOOTLOADER (device)) {
		g_string_append_printf (str, "  FlashAddrHigh:\t0x%04x\n",
					lu_device_bootloader_get_addr_hi (self));
		g_string_append_printf (str, "  FlashAddrLow:\t0x%04x\n",
					lu_device_bootloader_get_addr_lo (self));
		g_string_append_printf (str, "  FlashBlockSize:\t0x%04x\n",
					lu_device_bootloader_get_blocksize (self));
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

guint16
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

static const gchar *
lu_device_hidpp20_function_to_string (guint16 feature, guint8 function_id)
{
	if (feature == HIDPP_FEATURE_ROOT) {
		if (function_id == 0x00)
			return "getFeature";
		if (function_id == 0x01)
			return "ping";
		return NULL;
	}
	if (feature == HIDPP_FEATURE_I_FIRMWARE_INFO) {
		if (function_id == 0x00)
			return "getCount";
		if (function_id == 0x01)
			return "getInfo";
		return NULL;
	}
	if (feature == HIDPP_FEATURE_BATTERY_LEVEL_STATUS) {
		if (function_id == 0x00)
			return "GetBatteryLevelStatus";
		return NULL;
	}
	if (feature == HIDPP_FEATURE_DFU_CONTROL) {
		if (function_id == 0x00)
			return "getDfuControl";
		if (function_id == 0x01)
			return "setDfuControl";
		return NULL;
	}
	if (feature == HIDPP_FEATURE_DFU_CONTROL_SIGNED) {
		if (function_id == 0x00)
			return "getDfuStatus";
		if (function_id == 0x01)
			return "startDfu";
		return NULL;
	}
	if (feature == HIDPP_FEATURE_DFU) {
		if (function_id == 0x00)
			return "dfuCmdData0";
		if (function_id == 0x01)
			return "dfuCmdData1";
		if (function_id == 0x02)
			return "dfuCmdData2";
		if (function_id == 0x03)
			return "dfuCmdData3";
		if (function_id == 0x04)
			return "dfuStart";
		if (function_id == 0x05)
			return "restart";
		return NULL;
	}
	return NULL;
}

static gchar *
lu_device_hidpp_msg_to_string (LuDevice *device, LuHidppMsg *msg)
{
	GString *str = g_string_new (NULL);
	LuDevicePrivate *priv = GET_PRIVATE (device);
	const gchar *tmp;
	const gchar *kind_str = lu_device_kind_to_string (priv->kind);
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) flags_str = g_string_new (NULL);

	g_return_val_if_fail (msg != NULL, NULL);

	g_string_append_printf (str, "device-kind: %s\n", kind_str);
	if (msg->flags == LU_HIDPP_MSG_FLAG_NONE) {
		g_string_append (flags_str, "none");
	} else {
		if (msg->flags & LU_HIDPP_MSG_FLAG_LONGER_TIMEOUT)
			g_string_append (flags_str, "longer-timeout,");
		if (msg->flags & LU_HIDPP_MSG_FLAG_IGNORE_SUB_ID)
			g_string_append (flags_str, "ignore-sub-id,");
		if (msg->flags & LU_HIDPP_MSG_FLAG_IGNORE_FNCT_ID)
			g_string_append (flags_str, "ignore-fnct-id,");
		if (msg->flags & LU_HIDPP_MSG_FLAG_IGNORE_SWID)
			g_string_append (flags_str, "ignore-swid,");
		if (str->len > 0)
			g_string_truncate (str, str->len - 1);
	}
	g_string_append_printf (str, "flags:       %02x   [%s]\n",
				msg->flags,
				flags_str->str);
	g_string_append_printf (str, "report-id:   %02x   [%s]\n",
				msg->report_id,
				lu_hidpp_msg_rpt_id_to_string (msg));
	tmp = lu_hidpp_msg_dev_id_to_string (msg);
	g_string_append_printf (str, "device-id:   %02x   [%s]\n",
				msg->device_id, tmp );
	if (priv->hidpp_version >= 2.f) {
		guint16 feature = lu_device_hidpp_feature_find_by_idx (device, msg->sub_id);
		guint8 sw_id = msg->function_id & 0x0f;
		guint8 function_id = (msg->function_id & 0xf0) >> 4;
		g_string_append_printf (str, "feature:     %04x [%s]\n",
					feature,
					lu_hidpp_feature_to_string (feature));
		g_string_append_printf (str, "function-id: %02x   [%s]\n",
					function_id,
					lu_device_hidpp20_function_to_string (feature, function_id));
		g_string_append_printf (str, "sw-id:       %02x   [%s]\n",
					sw_id,
					sw_id == LU_HIDPP_MSG_SW_ID ? "fwupd" : "???");
	} else {
		g_string_append_printf (str, "sub-id:      %02x   [%s]\n",
					msg->sub_id,
					lu_hidpp_msg_sub_id_to_string (msg));
		g_string_append_printf (str, "function-id: %02x   [%s]\n",
					msg->function_id,
					lu_hidpp_msg_fcn_id_to_string (msg));
	}
	if (!lu_hidpp_msg_is_error (msg, &error)) {
		g_string_append_printf (str, "error:       %s\n",
					error->message);
	}
	return g_string_free (str, FALSE);
}

gboolean
lu_device_hidpp_send (LuDevice *device,
		      LuHidppMsg *msg,
		      guint timeout,
		      GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	gsize len = lu_hidpp_msg_get_payload_length (msg);

	/* only for HID++2.0 */
	if (lu_device_get_hidpp_version (device) >= 2.f)
		msg->function_id |= LU_HIDPP_MSG_SW_ID;

	lu_device_hidpp_dump (device, "host->device", (guint8 *) msg, len);

	/* detailed debugging */
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
		g_autofree gchar *str = lu_device_hidpp_msg_to_string (device, msg);
		g_print ("%s", str);
	}

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
			 LuHidppMsg *msg,
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
						      sizeof(LuHidppMsg),
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
				       sizeof(LuHidppMsg),
				       &read_size,
				       timeout,
				       error)) {
			g_prefix_error (error, "failed to receive: ");
			return FALSE;
		}
	}

	/* check long enough, but allow returning oversize packets */
	lu_device_hidpp_dump (device, "device->host", (guint8 *) msg, read_size);
	if (read_size < lu_hidpp_msg_get_payload_length (msg)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "message length too small, "
			     "got %" G_GSIZE_FORMAT " expected %" G_GSIZE_FORMAT,
			     read_size, lu_hidpp_msg_get_payload_length (msg));
		return FALSE;
	}

	/* detailed debugging */
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
		g_autofree gchar *str = lu_device_hidpp_msg_to_string (device, msg);
		g_print ("%s", str);
	}

	/* success */
	return TRUE;
}

gboolean
lu_device_hidpp_transfer (LuDevice *device, LuHidppMsg *msg, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	guint timeout = LU_DEVICE_TIMEOUT_MS;
	g_autoptr(LuHidppMsg) msg_tmp = lu_hidpp_msg_new ();

	/* increase timeout for some operations */
	if (msg->flags & LU_HIDPP_MSG_FLAG_LONGER_TIMEOUT)
		timeout *= 10;

	/* send request */
	if (!lu_device_hidpp_send (device, msg, timeout, error))
		return FALSE;

	/* keep trying to receive until we get a valid reply */
	while (1) {
		if (!lu_device_hidpp_receive (device, msg_tmp, timeout, error))
			return FALSE;

		/* we don't know how to handle this report packet */
		if (lu_hidpp_msg_get_payload_length (msg_tmp) == 0x0) {
			g_debug ("HID++1.0 report 0x%02x has unknown length, ignoring",
				 msg_tmp->report_id);
			continue;
		}

		if (!lu_hidpp_msg_is_error (msg_tmp, error))
			return FALSE;

		/* is valid reply */
		if (lu_hidpp_msg_is_reply (msg, msg_tmp))
			break;

		/* to ensure compatibility when an HID++ 2.0 device is
		 * connected to an HID++ 1.0 receiver, any feature index
		 * corresponding to an HID++ 1.0 sub-identifier which could be
		 * sent by the receiver, must be assigned to a dummy feature */
		if (lu_device_get_hidpp_version (device) >= 2.f) {
			if (lu_hidpp_msg_is_hidpp10_compat (msg_tmp)) {
				g_debug ("ignoring HID++1.0 reply");
				continue;
			}

			/* not us */
			if ((msg->flags & LU_HIDPP_MSG_FLAG_IGNORE_SWID) == 0) {
				if (!lu_hidpp_msg_verify_swid (msg_tmp)) {
					g_debug ("ignoring reply with SwId 0x%02i, expected 0x%02i",
						 msg_tmp->function_id & 0x0f,
						 LU_HIDPP_MSG_SW_ID);
					continue;
				}
			}
		}

		g_debug ("ignoring message");

	};

	/* if the HID++ ID is unset, grab it from the reply */
	if (priv->hidpp_id == HIDPP_DEVICE_ID_UNSET) {
		priv->hidpp_id = msg_tmp->device_id;
		g_debug ("HID++ ID now %02x", priv->hidpp_id);
	}

	/* copy over data */
	lu_hidpp_msg_copy (msg, msg_tmp);
	return TRUE;
}

gboolean
lu_device_hidpp_feature_search (LuDevice *device, guint16 feature, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	LuDeviceHidppMap *map;
	g_autoptr(LuHidppMsg) msg = lu_hidpp_msg_new ();

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

gdouble
lu_device_get_hidpp_version (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->hidpp_version;
}

void
lu_device_set_hidpp_version (LuDevice *device, gdouble hidpp_version)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->hidpp_version = hidpp_version;
}

const gchar *
lu_device_get_version_hw (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	return priv->version_hw;
}

void
lu_device_set_version_hw (LuDevice *device, const gchar *version_hw)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_free (priv->version_hw);
	priv->version_hw = g_strdup (version_hw);
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
	LuDevicePrivate *priv = GET_PRIVATE (device);

	/* clear the feature map (leaving only the root) */
	g_ptr_array_set_size (priv->feature_index, 0);

	/* probe the hardware */
	if (klass->probe != NULL)
		return klass->probe (device, error);
	return TRUE;
}

gboolean
lu_device_open (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);
	LuDevicePrivate *priv = GET_PRIVATE (device);
	g_autofree gchar *device_str = NULL;

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (lu_device_has_flag (device, LU_DEVICE_FLAG_IS_OPEN))
		return TRUE;

	/* set default vendor */
	fu_device_set_vendor (FU_DEVICE (device), "Logitech");

	/* USB */
	if (priv->usb_device != NULL) {
		guint8 num_interfaces = 0x01;
		g_autofree gchar *devid = NULL;

		/* open device */
		if (priv->usb_device_locker == NULL) {
			g_autoptr(FuDeviceLocker) locker = NULL;
			g_debug ("opening unifying device using USB");
			locker = fu_device_locker_new (priv->usb_device, error);
			if (locker == NULL)
				return FALSE;
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
			priv->usb_device_locker = g_steal_pointer (&locker);
		}

		/* generate GUID */
		devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
					 g_usb_device_get_vid (priv->usb_device),
					 g_usb_device_get_pid (priv->usb_device));
		fu_device_add_guid (FU_DEVICE (device), devid);

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

	/* add known root for HID++2.0 */
	if (lu_device_get_hidpp_version (device) >= 2.f) {
		LuDeviceHidppMap *map = g_new0 (LuDeviceHidppMap, 1);
		map->idx = 0x00;
		map->feature = HIDPP_FEATURE_ROOT;
		g_ptr_array_add (priv->feature_index, map);
	}

	/* show the device */
	device_str = fu_device_to_string (FU_DEVICE (device));
	g_debug ("%s", device_str);

	/* success */
	return TRUE;
}

gboolean
lu_device_poll (LuDevice *device, GError **error)
{
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);
	if (klass->poll != NULL) {
		if (!klass->poll (device, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * lu_device_close:
 * @device: A #LuDevice
 * @error: A #GError, or %NULL
 *
 * Closes the device.
 *
 * Returns: %TRUE for success
 **/
gboolean
lu_device_close (LuDevice *device, GError **error)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not open */
	if (!lu_device_has_flag (device, LU_DEVICE_FLAG_IS_OPEN))
		return TRUE;

	/* subclassed */
	g_debug ("closing device");
	if (klass->close != NULL) {
		if (!klass->close (device, error))
			return FALSE;
	}

	/* USB */
	if (priv->usb_device_locker != NULL) {
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
		g_clear_object (&priv->usb_device_locker);
	}
	g_clear_object (&priv->usb_device);

	/* HID */
	if (priv->udev_device != NULL && priv->udev_device_fd > 0) {
		if (!g_close (priv->udev_device_fd, error))
			return FALSE;
		priv->udev_device_fd = 0;
	}

	/* success */
	lu_device_remove_flag (device, LU_DEVICE_FLAG_IS_OPEN);
	return TRUE;
}
static gboolean
lu_device_detach (FuDevice *device, GError **error)
{
	LuDevice *self = LU_DEVICE (device);
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	g_debug ("detaching device");
	if (klass->detach != NULL)
		return klass->detach (self, error);

	/* nothing to do */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "device detach is not supported");
	return FALSE;
}

static gboolean
lu_device_attach (FuDevice *device, GError **error)
{
	LuDevice *self = LU_DEVICE (device);
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check kind */
	if (lu_device_get_kind (self) == LU_DEVICE_KIND_RUNTIME) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "device is not in bootloader state");
		return FALSE;
	}

	/* subclassed */
	if (klass->attach != NULL)
		return klass->attach (self, error);

	return TRUE;
}

static gboolean
lu_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	LuDevice *self = LU_DEVICE (device);
	LuDeviceClass *klass = LU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* call device-specific method */
	if (klass->write_firmware == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "not supported in %s",
			     lu_device_kind_to_string (lu_device_get_kind (self)));
		return FALSE;
	}

	/* call either nordic or texas vfunc */
	return klass->write_firmware (self, fw, error);
}

#ifndef HAVE_GUDEV_232
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)
#pragma clang diagnostic pop
#endif

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
	return NULL;
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
			fu_device_set_platform_id (FU_DEVICE (device), tmp);
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
	if (priv->usb_device_locker != NULL)
		g_object_unref (priv->usb_device_locker);
	if (priv->udev_device != NULL)
		g_object_unref (priv->udev_device);
	if (priv->udev_device_fd > 0)
		g_close (priv->udev_device_fd, NULL);
	g_ptr_array_unref (priv->feature_index);
	g_free (priv->version_hw);

	G_OBJECT_CLASS (lu_device_parent_class)->finalize (object);
}

static void
lu_device_init (LuDevice *device)
{
	LuDevicePrivate *priv = GET_PRIVATE (device);
	priv->hidpp_id = HIDPP_DEVICE_ID_UNSET;
	priv->feature_index = g_ptr_array_new_with_free_func (g_free);
	fu_device_set_vendor_id (FU_DEVICE (device), "USB:0x046D");
}

static void
lu_device_class_init (LuDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);

	object_class->finalize = lu_device_finalize;
	object_class->get_property = lu_device_get_property;
	object_class->set_property = lu_device_set_property;
	klass_device->to_string = lu_device_to_string;
	klass_device->write_firmware = lu_device_write_firmware;
	klass_device->attach = lu_device_attach;
	klass_device->detach = lu_device_detach;

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
