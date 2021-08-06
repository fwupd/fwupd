/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-hidpp.h"

typedef struct {
	guint8			 cached_fw_entity;
	guint8			 hidpp_id;
	guint8			 hidpp_version;
	gboolean		 is_updatable;
	gboolean		 is_active;
	FuIOChannel		*io_channel;
	GPtrArray		*feature_index;	/* of FuLogitechHidPpHidppMap */
} FuLogitechHidPpDevicePrivate;

typedef struct {
	guint8			 idx;
	guint16			 feature;
} FuLogitechHidPpHidppMap;

G_DEFINE_TYPE_WITH_PRIVATE (FuLogitechHidPpDevice, fu_logitech_hidpp_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_logitech_hidpp_device_get_instance_private (o))

typedef enum {
	FU_HIDPP_DEVICE_KIND_KEYBOARD,
	FU_HIDPP_DEVICE_KIND_REMOTE_CONTROL,
	FU_HIDPP_DEVICE_KIND_NUMPAD,
	FU_HIDPP_DEVICE_KIND_MOUSE,
	FU_HIDPP_DEVICE_KIND_TOUCHPAD,
	FU_HIDPP_DEVICE_KIND_TRACKBALL,
	FU_HIDPP_DEVICE_KIND_PRESENTER,
	FU_HIDPP_DEVICE_KIND_RECEIVER,
	FU_HIDPP_DEVICE_KIND_LAST
} FuLogitechHidPpDeviceKind;

void
fu_logitech_hidpp_device_set_hidpp_id (FuLogitechHidPpDevice *self, guint8 hidpp_id)
{
	FuLogitechHidPpDevicePrivate *priv;
	g_return_if_fail (FU_IS_HIDPP_DEVICE (self));
	priv = GET_PRIVATE (self);
	priv->hidpp_id = hidpp_id;
}

static const gchar *
fu_logitech_hidpp_device_get_icon (FuLogitechHidPpDeviceKind kind)
{
	if (kind == FU_HIDPP_DEVICE_KIND_KEYBOARD)
		return "input-keyboard";
	if (kind == FU_HIDPP_DEVICE_KIND_REMOTE_CONTROL)
		return "pda"; // ish
	if (kind == FU_HIDPP_DEVICE_KIND_NUMPAD)
		return "input-dialpad";
	if (kind == FU_HIDPP_DEVICE_KIND_MOUSE)
		return "input-mouse";
	if (kind == FU_HIDPP_DEVICE_KIND_TOUCHPAD)
		return "input-touchpad";
	if (kind == FU_HIDPP_DEVICE_KIND_TRACKBALL)
		return "input-mouse"; // ish
	if (kind == FU_HIDPP_DEVICE_KIND_PRESENTER)
		return "pda"; // ish
	if (kind == FU_HIDPP_DEVICE_KIND_RECEIVER)
		return "preferences-desktop-keyboard";
	return NULL;
}

static const gchar *
fu_logitech_hidpp_device_get_summary (FuLogitechHidPpDeviceKind kind)
{
	if (kind == FU_HIDPP_DEVICE_KIND_KEYBOARD)
		return "Unifying Keyboard";
	if (kind == FU_HIDPP_DEVICE_KIND_REMOTE_CONTROL)
		return "Unifying Remote Control";
	if (kind == FU_HIDPP_DEVICE_KIND_NUMPAD)
		return "Unifying Number Pad";
	if (kind == FU_HIDPP_DEVICE_KIND_MOUSE)
		return "Unifying Mouse";
	if (kind == FU_HIDPP_DEVICE_KIND_TOUCHPAD)
		return "Unifying Touchpad";
	if (kind == FU_HIDPP_DEVICE_KIND_TRACKBALL)
		return "Unifying Trackball";
	if (kind == FU_HIDPP_DEVICE_KIND_PRESENTER)
		return "Unifying Presenter";
	if (kind == FU_HIDPP_DEVICE_KIND_RECEIVER)
		return "Unifying Receiver";
	return NULL;
}

static const gchar *
fu_logitech_hidpp_feature_to_string (guint16 feature)
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

static void
fu_logitech_hidpp_device_refresh_updatable (FuLogitechHidPpDevice *self)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);

	/* device can only be upgraded if it is capable, and active */
	if (priv->is_updatable && priv->is_active) {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
		return;
	}
	fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static gboolean
fu_logitech_hidpp_device_ping (FuLogitechHidPpDevice *self, GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	gdouble version;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

	/* handle failure */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->hidpp_id;
	msg->sub_id = 0x00; /* rootIndex */
	msg->function_id = 0x01 << 4; /* ping */
	msg->data[0] = 0x00;
	msg->data[1] = 0x00;
	msg->data[2] = 0xaa; /* user-selected value */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, &error_local)) {
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED)) {
			priv->hidpp_version = 1;
			return TRUE;
		}
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_HOST_UNREACHABLE)) {
			priv->is_active = FALSE;
			fu_logitech_hidpp_device_refresh_updatable (self);
			return TRUE;
		}
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to ping %s: %s",
			     fu_device_get_name (FU_DEVICE (self)),
			     error_local->message);
		return FALSE;
	}

	/* device no longer asleep */
	priv->is_active = TRUE;
	fu_logitech_hidpp_device_refresh_updatable (self);

	/* if the HID++ ID is unset, grab it from the reply */
	if (priv->hidpp_id == HIDPP_DEVICE_ID_UNSET &&
	    msg->device_id != HIDPP_DEVICE_ID_UNSET) {
		priv->hidpp_id = msg->device_id;
		g_debug ("HID++ ID is %02x", priv->hidpp_id);
	}

	/* format version in BCD format */
	version = (gdouble) msg->data[0] + ((gdouble) msg->data[1]) / 100.f;
	priv->hidpp_version = (guint) version;

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_close (FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);

	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown (priv->io_channel, error))
			return FALSE;
		g_clear_object (&priv->io_channel);
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_poll (FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new (self, error);
	if (locker == NULL)
		return FALSE;

	/* flush pending data */
	msg->device_id = priv->hidpp_id;
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_receive (priv->io_channel, msg, timeout, &error_local)) {
		if (!g_error_matches (error_local,
				      G_IO_ERROR,
				      G_IO_ERROR_TIMED_OUT)) {
			g_warning ("failed to get pending read: %s", error_local->message);
			return TRUE;
		}
		/* no data to receive */
		g_clear_error (&error_local);
	}

	/* just ping */
	if (!fu_logitech_hidpp_device_ping (self, &error_local)) {
		g_warning ("failed to ping device: %s", error_local->message);
		return TRUE;
	}

	/* this is the first time the device has been active */
	if (priv->feature_index->len == 0) {
		fu_device_probe_invalidate (FU_DEVICE (self));
		if (!fu_device_setup (FU_DEVICE (self), error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_open (FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));
	const gchar *devpath = g_udev_device_get_device_file (udev_device);

	/* open */
	priv->io_channel = fu_io_channel_new_file (devpath, error);
	if (priv->io_channel == NULL)
		return FALSE;

	return TRUE;
}

static void
fu_logitech_hidpp_map_to_string (FuLogitechHidPpHidppMap *map, guint idt, GString *str)
{
	g_autofree gchar *title = g_strdup_printf ("Feature%02x", map->idx);
	g_autofree gchar *tmp = g_strdup_printf ("%s [0x%04x]",
						 fu_logitech_hidpp_feature_to_string (map->feature),
						 map->feature);
	fu_common_string_append_kv (str, idt, title, tmp);
}

static void
fu_logitech_hidpp_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS (fu_logitech_hidpp_device_parent_class)->to_string (device, idt, str);

	fu_common_string_append_ku (str, idt, "HidppVersion", priv->hidpp_version);
	fu_common_string_append_kx (str, idt, "HidppId", priv->hidpp_id);
	fu_common_string_append_kb (str, idt, "IsUpdatable", priv->is_updatable);
	fu_common_string_append_kb (str, idt, "IsActive", priv->is_active);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		FuLogitechHidPpHidppMap *map = g_ptr_array_index (priv->feature_index, i);
		fu_logitech_hidpp_map_to_string (map, idt, str);
	}
}

static guint8
fu_logitech_hidpp_device_feature_get_idx (FuLogitechHidPpDevice *self, guint16 feature)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);

	for (guint i = 0; i < priv->feature_index->len; i++) {
		FuLogitechHidPpHidppMap *map = g_ptr_array_index (priv->feature_index, i);
		if (map->feature == feature)
			return map->idx;
	}
	return 0x00;
}

static gboolean
fu_logitech_hidpp_device_fetch_firmware_info (FuLogitechHidPpDevice *self, GError **error)
{
	guint8 idx;
	guint8 entity_count;
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get the entity count */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->hidpp_id;
	msg->sub_id = idx;
	msg->function_id = 0x00 << 4; /* getCount */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
		g_prefix_error (error, "failed to get firmware count: ");
		return FALSE;
	}
	entity_count = msg->data[0];
	g_debug ("firmware entity count is %u", entity_count);

	/* get firmware, bootloader, hardware versions */
	for (guint8 i = 0; i < entity_count; i++) {
		guint16 build;
		g_autofree gchar *version = NULL;
		g_autofree gchar *name = NULL;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->hidpp_id;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* getInfo */
		msg->data[0] = i;
		if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
			g_prefix_error (error, "failed to get firmware info: ");
			return FALSE;
		}
		if (msg->data[1] == 0x00 &&
		    msg->data[2] == 0x00 &&
		    msg->data[3] == 0x00 &&
		    msg->data[4] == 0x00 &&
		    msg->data[5] == 0x00 &&
		    msg->data[6] == 0x00 &&
		    msg->data[7] == 0x00) {
			g_debug ("no version set for entity %u", i);
			continue;
		}
		name = g_strdup_printf ("%c%c%c",
					msg->data[1],
					msg->data[2],
					msg->data[3]);
		build = ((guint16) msg->data[6]) << 8 | msg->data[7];
		version = fu_logitech_hidpp_format_version (name,
					     msg->data[4],
					     msg->data[5],
					     build);
		g_debug ("firmware entity 0x%02x version is %s", i, version);
		if (msg->data[0] == 0) {
			fu_device_set_version (FU_DEVICE (self), version);
			priv->cached_fw_entity = i;
		} else if (msg->data[0] == 1) {
			fu_device_set_version_bootloader (FU_DEVICE (self), version);
		} else if (msg->data[0] == 2) {
			fu_device_set_metadata (FU_DEVICE (self), "version-hw", version);
		}
	}

	/* not an error, the device just doesn't support this */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_fetch_battery_level (FuLogitechHidPpDevice *self, GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);

	/* try using HID++2.0 */
	if (priv->hidpp_version >= 2.f) {
		guint8 idx;
		idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_BATTERY_LEVEL_STATUS);
		if (idx != 0x00) {
			g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
			msg->report_id = HIDPP_REPORT_ID_SHORT;
			msg->device_id = priv->hidpp_id;
			msg->sub_id = idx;
			msg->function_id = 0x00; /* GetBatteryLevelStatus */
			msg->hidpp_version = priv->hidpp_version;
			if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
				g_prefix_error (error, "failed to get battery info: ");
				return FALSE;
			}
			if (msg->data[0] != 0x00)
				fu_device_set_battery_level (FU_DEVICE (self), msg->data[0]);
			return TRUE;
		}
	}

	/* try HID++1.0 battery mileage */
	if (priv->hidpp_version == 1.f) {
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->hidpp_id;
		msg->sub_id = HIDPP_SUBID_GET_REGISTER;
		msg->function_id = HIDPP_REGISTER_BATTERY_MILEAGE;
		msg->hidpp_version = priv->hidpp_version;
		if (fu_logitech_hidpp_transfer (priv->io_channel, msg, NULL)) {
			if (msg->data[0] != 0x00)
				fu_device_set_battery_level (FU_DEVICE (self), msg->data[0]);
			return TRUE;
		}

		/* try HID++1.0 battery status instead */
		msg->function_id = HIDPP_REGISTER_BATTERY_STATUS;
		if (fu_logitech_hidpp_transfer (priv->io_channel, msg, NULL)) {
			switch (msg->data[0]) {
			case 1: /* 0 - 10 */
				fu_device_set_battery_level (FU_DEVICE (self), 5);
				break;
			case 3: /* 11 - 30 */
				fu_device_set_battery_level (FU_DEVICE (self), 20);
				break;
			case 5: /* 31 - 80 */
				fu_device_set_battery_level (FU_DEVICE (self), 55);
				break;
			case 7: /* 81 - 100 */
				fu_device_set_battery_level (FU_DEVICE (self), 90);
				break;
			default:
				g_warning ("unknown battery percentage: 0x%02x",
					   msg->data[0]);
				break;
			}
			return TRUE;
		}
	}

	/* not an error, the device just doesn't support any of the methods */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_feature_search (FuDevice *device, guint16 feature, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	FuLogitechHidPpHidppMap *map;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

	/* find the idx for the feature */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->hidpp_id;
	msg->sub_id = 0x00; /* rootIndex */
	msg->function_id = 0x00 << 4; /* getFeature */
	msg->data[0] = feature >> 8;
	msg->data[1] = feature;
	msg->data[2] = 0x00;
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
		g_prefix_error (error,
				"failed to get idx for feature %s [0x%04x]: ",
				fu_logitech_hidpp_feature_to_string (feature), feature);
		return FALSE;
	}

	/* zero index */
	if (msg->data[0] == 0x00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "feature %s [0x%04x] not found",
			     fu_logitech_hidpp_feature_to_string (feature), feature);
		return FALSE;
	}

	/* add to map */
	map = g_new0 (FuLogitechHidPpHidppMap, 1);
	map->idx = msg->data[0];
	map->feature = feature;
	g_ptr_array_add (priv->feature_index, map);
	g_debug ("added feature %s [0x%04x] as idx %02x",
		 fu_logitech_hidpp_feature_to_string (feature), feature, map->idx);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_probe (FuDevice *device, GError **error)
{
	g_autofree gchar *devid = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS (fu_logitech_hidpp_device_parent_class)->probe (device, error))
		return FALSE;

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error))
		return FALSE;

	/* nearly... */
	fu_device_add_vendor_id (device, "USB:0x046D");

	/* this is a non-standard extension */
	devid = g_strdup_printf ("UFY\\VID_%04X&PID_%04X",
				 fu_udev_device_get_vendor (FU_UDEV_DEVICE (device)),
				 fu_udev_device_get_model (FU_UDEV_DEVICE (device)));
	fu_device_add_instance_id (device, devid);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_setup (FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	guint8 idx;
	const guint16 map_features[] = {
		HIDPP_FEATURE_GET_DEVICE_NAME_TYPE,
		HIDPP_FEATURE_I_FIRMWARE_INFO,
		HIDPP_FEATURE_BATTERY_LEVEL_STATUS,
		HIDPP_FEATURE_DFU_CONTROL,
		HIDPP_FEATURE_DFU_CONTROL_SIGNED,
		HIDPP_FEATURE_DFU,
		HIDPP_FEATURE_ROOT };

	/* ping device to get HID++ version */
	if (!fu_logitech_hidpp_device_ping (self, error))
		return FALSE;

	/* did not get ID */
	if (priv->hidpp_id == HIDPP_DEVICE_ID_UNSET) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no HID++ ID");
		return FALSE;
	}

	/* add known root for HID++2.0 */
	g_ptr_array_set_size (priv->feature_index, 0);
	if (priv->hidpp_version >= 2.f) {
		FuLogitechHidPpHidppMap *map = g_new0 (FuLogitechHidPpHidppMap, 1);
		map->idx = 0x00;
		map->feature = HIDPP_FEATURE_ROOT;
		g_ptr_array_add (priv->feature_index, map);
	}

	/* map some *optional* HID++2.0 features we might use */
	for (guint i = 0; map_features[i] != HIDPP_FEATURE_ROOT; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_logitech_hidpp_feature_search (device,
						       map_features[i],
						       &error_local)) {
			g_debug ("%s", error_local->message);
			if (g_error_matches (error_local,
					     G_IO_ERROR,
					     G_IO_ERROR_TIMED_OUT) ||
			    g_error_matches (error_local,
					     G_IO_ERROR,
					     G_IO_ERROR_HOST_UNREACHABLE)) {
				/* timed out, so not trying any more */
				break;
			}
		}
	}

	/* get the firmware information */
	if (!fu_logitech_hidpp_device_fetch_firmware_info (self, error))
		return FALSE;

	/* get the battery level */
	if (!fu_logitech_hidpp_device_fetch_battery_level (self, error))
		return FALSE;

	/* try using HID++2.0 */
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_GET_DEVICE_NAME_TYPE);
	if (idx != 0x00) {
		const gchar *tmp;
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->hidpp_id;
		msg->sub_id = idx;
		msg->function_id = 0x02 << 4; /* getDeviceType */
		msg->hidpp_version = priv->hidpp_version;
		if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
			g_prefix_error (error, "failed to get device type: ");
			return FALSE;
		}

		/* add nice-to-have data */
		tmp = fu_logitech_hidpp_device_get_summary (msg->data[0]);
		if (tmp != NULL)
			fu_device_set_summary (FU_DEVICE (device), tmp);
		tmp = fu_logitech_hidpp_device_get_icon (msg->data[0]);
		if (tmp != NULL)
			fu_device_add_icon (FU_DEVICE (device), tmp);
	}
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		priv->is_updatable = TRUE;
		fu_device_remove_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_add_protocol (FU_DEVICE (self), "com.logitech.unifying");
	}
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		/* check the feature is available */
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->hidpp_id;
		msg->sub_id = idx;
		msg->function_id = 0x00 << 4; /* getDfuStatus */
		msg->hidpp_version = priv->hidpp_version;
		if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
			g_prefix_error (error, "failed to get DFU status: ");
			return FALSE;
		}
		if ((msg->data[2] & 0x01) > 0) {
			g_warning ("DFU mode not available");
		} else {
			priv->is_updatable = TRUE;
			fu_device_remove_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		}
		fu_device_add_protocol (FU_DEVICE (device), "com.logitech.unifyingsigned");
	}
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU);
	if (idx != 0x00) {
		priv->is_updatable = TRUE;
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		if (fu_device_get_version (device) == NULL) {
			g_debug ("repairing device in bootloader mode");
			fu_device_set_version (FU_DEVICE (device), "MPK00.00_B0000");
		}
		/* we do not actually know which protocol when in recovery mode,
		 * so force the metadata to have the specific regex set up */
		fu_device_add_protocol (FU_DEVICE (self), "com.logitech.unifying");
		fu_device_add_protocol (FU_DEVICE (self), "com.logitech.unifyingsigned");
	}

	/* this device may have changed state */
	fu_logitech_hidpp_device_refresh_updatable (self);

	/* poll for pings to track active state */
	fu_device_set_poll_interval (device, 30000);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_detach (FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	guint8 idx;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

	/* sanity check */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	/* this requires user action */
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		msg->report_id = HIDPP_REPORT_ID_LONG;
		msg->device_id = priv->hidpp_id;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* setDfuControl */
		msg->data[0] = 0x01; /* enterDfu */
		msg->data[1] = 0x00; /* dfuControlParam */
		msg->data[2] = 0x00; /* unused */
		msg->data[3] = 0x00; /* unused */
		msg->data[4] = 'D';
		msg->data[5] = 'F';
		msg->data[6] = 'U';
		msg->hidpp_version = priv->hidpp_version;
		msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
			     FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
		if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
			g_prefix_error (error, "failed to put device into DFU mode: ");
			return FALSE;
		}
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NEEDS_USER_ACTION,
			     "%s needs to be manually restarted to complete the update."
			     "Please unplug and reconnect the device and re-run the update",
			     fu_device_get_name (device));
		return FALSE;
	}

	/* this can reboot all by itself */
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		msg->report_id = HIDPP_REPORT_ID_LONG;
		msg->device_id = priv->hidpp_id;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* setDfuControl */
		msg->data[0] = 0x01; /* startDfu */
		msg->data[1] = 0x00; /* dfuControlParam */
		msg->data[2] = 0x00; /* unused */
		msg->data[3] = 0x00; /* unused */
		msg->data[4] = 'D';
		msg->data[5] = 'F';
		msg->data[6] = 'U';
		msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID;
		if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
			g_prefix_error (error, "failed to put device into DFU mode: ");
			return FALSE;
		}
		g_usleep (200 * 1000);
		return fu_logitech_hidpp_device_setup (FU_DEVICE (self), error);
	}

	/* we don't know how */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_FAILED,
		     "no method to detach");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_check_status (guint8 status, GError **error)
{
	switch (status & 0x7f) {
	case 0x00:
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "invalid status value 0x%02x",
			     status);
		break;
	case 0x01: /* packet success */
	case 0x02: /* DFU success */
	case 0x05: /* DFU success: entity restart required */
	case 0x06: /* DFU success: system restart required */
		/* success */
		return TRUE;
		break;
	case 0x03:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PENDING,
				     "wait for event (command in progress)");
		break;
	case 0x04:
	case 0x10: /* unknown */
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "generic error");
		break;
	case 0x11:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "bad voltage (power too low?)");
		break;
	case 0x12:
	case 0x14: /* bad magic string */
	case 0x21: /* bad firmware */
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unsupported firmware");
		break;
	case 0x13:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unsupported encryption mode");
		break;
	case 0x15:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "erase failure");
		break;
	case 0x16:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "DFU not started");
		break;
	case 0x17:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "bad sequence number");
		break;
	case 0x18:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unsupported command");
		break;
	case 0x19:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "command in progress");
		break;
	case 0x1a:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "address out of range");
		break;
	case 0x1b:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unaligned address");
		break;
	case 0x1c:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "bad size");
		break;
	case 0x1d:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "missing program data");
		break;
	case 0x1e:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "missing check data");
		break;
	case 0x1f:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "program failed to write");
		break;
	case 0x20:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "program failed to verify");
		break;
	case 0x22:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "firmware check failure");
		break;
	case 0x23:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "blocked command (restart required)");
		break;
	default:
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "unhandled status value 0x%02x",
			     status);
		break;
	}
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_pkt (FuLogitechHidPpDevice *self,
					   guint8 idx,
					   guint8 cmd,
					   const guint8 *data,
					   GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	guint32 packet_cnt;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
	g_autoptr(GError) error_local = NULL;

	/* send firmware data */
	msg->report_id = HIDPP_REPORT_ID_LONG;
	msg->device_id = priv->hidpp_id;
	msg->sub_id = idx;
	msg->function_id = cmd << 4; /* dfuStart or dfuCmdDataX */
	msg->hidpp_version = priv->hidpp_version;
	memcpy (msg->data, data, 16);
	if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, &error_local)) {
		g_prefix_error (error, "failed to supply program data: ");
		return FALSE;
	}

	/* check error */
	if (!fu_common_read_uint32_safe (msg->data, sizeof(msg->data), 0x0,
					 &packet_cnt, G_BIG_ENDIAN, error))
		return FALSE;
	g_debug ("packet_cnt=0x%04x", packet_cnt);
	if (fu_logitech_hidpp_device_check_status (msg->data[4], &error_local))
		return TRUE;

	/* fatal error */
	if (!g_error_matches (error_local,
			      G_IO_ERROR,
			      G_IO_ERROR_PENDING)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     error_local->message);
		return FALSE;
	}

	/* wait for the HID++ notification */
	g_debug ("ignoring: %s", error_local->message);
	for (guint retry = 0; retry < 10; retry++) {
		g_autoptr(FuLogitechHidPpHidppMsg) msg2 = fu_logitech_hidpp_msg_new ();
		msg2->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID;
		if (!fu_logitech_hidpp_receive (priv->io_channel, msg2, 15000, error))
			return FALSE;
		if (fu_logitech_hidpp_msg_is_reply (msg, msg2)) {
			g_autoptr(GError) error2 = NULL;
			if (!fu_logitech_hidpp_device_check_status (msg2->data[4], &error2)) {
				g_debug ("got %s, waiting a bit longer", error2->message);
				continue;
			}
			return TRUE;
		} else {
			g_debug ("got wrong packet, continue to wait...");
		}
	}

	/* nothing in the queue */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to get event after timeout");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	gsize sz = 0;
	const guint8 *data;
	guint8 cmd = 0x04;
	guint8 idx;
	g_autoptr(GBytes) fw = NULL;

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "no DFU feature available");
		return FALSE;
	}

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* flash hardware */
	data = g_bytes_get_data (fw, &sz);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (gsize i = 0; i < sz / 16; i++) {

		/* send packet and wait for reply */
		g_debug ("send data at addr=0x%04x", (guint) i * 16);
		if (!fu_logitech_hidpp_device_write_firmware_pkt (self,
								idx,
								cmd,
								data + (i * 16),
								error)) {
			g_prefix_error (error,
					"failed to write @0x%04x: ",
					(guint) i * 16);
			return FALSE;
		}

		/* use sliding window */
		cmd = (cmd + 1) % 4;

		/* update progress-bar */
		fu_progress_set_percentage_full(progress, i * 16, sz);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_attach (FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	guint8 idx;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

	/* sanity check */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx (self, HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "no DFU feature available");
		return FALSE;
	}

	/* reboot back into firmware mode */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->hidpp_id;
	msg->sub_id = idx;
	msg->function_id = 0x05 << 4; /* restart */
	msg->data[0] = priv->cached_fw_entity; /* fwEntity */
	msg->hidpp_version = priv->hidpp_version;
	msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
		     FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SWID | // inferred?
		     FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_transfer (priv->io_channel, msg, error)) {
		g_prefix_error (error, "failed to restart device: ");
		return FALSE;
	}

	/* reprobe */
	if (!fu_logitech_hidpp_device_setup (device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_logitech_hidpp_device_finalize (GObject *object)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE (object);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	g_ptr_array_unref (priv->feature_index);
	G_OBJECT_CLASS (fu_logitech_hidpp_device_parent_class)->finalize (object);
}

static void
fu_logitech_hidpp_device_class_init (FuLogitechHidPpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = fu_logitech_hidpp_device_finalize;
	klass_device->setup = fu_logitech_hidpp_device_setup;
	klass_device->open = fu_logitech_hidpp_device_open;
	klass_device->close = fu_logitech_hidpp_device_close;
	klass_device->write_firmware = fu_logitech_hidpp_device_write_firmware;
	klass_device->attach = fu_logitech_hidpp_device_attach;
	klass_device->detach = fu_logitech_hidpp_device_detach;
	klass_device->poll = fu_logitech_hidpp_device_poll;
	klass_device->to_string = fu_logitech_hidpp_device_to_string;
	klass_device->probe = fu_logitech_hidpp_device_probe;
}

static void
fu_logitech_hidpp_device_init (FuLogitechHidPpDevice *self)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE (self);
	priv->hidpp_id = HIDPP_DEVICE_ID_UNSET;
	priv->feature_index = g_ptr_array_new_with_free_func (g_free);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PLAIN);
}
