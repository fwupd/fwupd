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
#include "fu-logitech-hidpp-radio.h"
#include "fu-logitech-hidpp-runtime-bolt.h"

typedef struct {
	guint8 cached_fw_entity;
	/*
	 * Device index:
	 *   - HIDPP_DEVICE_IDX_RECEIVER for the receiver
	 *   - HIDPP_DEVICE_IDX_BLE for BLE devices
	 *   - pairing slot for paired Bolt devices.
	 */
	guint8 device_idx;
	guint16 hidpp_pid;
	guint8 hidpp_version;
	FuIOChannel *io_channel;
	gchar *model_id;
	GPtrArray *feature_index; /* of FuLogitechHidPpHidppMap */
} FuLogitechHidPpDevicePrivate;

typedef struct {
	guint8 idx;
	guint16 feature;
} FuLogitechHidPpHidppMap;

G_DEFINE_TYPE_WITH_PRIVATE(FuLogitechHidPpDevice, fu_logitech_hidpp_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_logitech_hidpp_device_get_instance_private(o))

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
fu_logitech_hidpp_device_set_device_idx(FuLogitechHidPpDevice *self, guint8 device_idx)
{
	FuLogitechHidPpDevicePrivate *priv;
	g_return_if_fail(FU_IS_HIDPP_DEVICE(self));
	priv = GET_PRIVATE(self);
	priv->device_idx = device_idx;
}

guint16
fu_logitech_hidpp_device_get_hidpp_pid(FuLogitechHidPpDevice *self)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_HIDPP_DEVICE(self), G_MAXUINT16);
	return priv->hidpp_pid;
}

void
fu_logitech_hidpp_device_set_hidpp_pid(FuLogitechHidPpDevice *self, guint16 hidpp_pid)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_HIDPP_DEVICE(self));
	priv->hidpp_pid = hidpp_pid;
}

const gchar *
fu_logitech_hidpp_device_get_model_id(FuLogitechHidPpDevice *self)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_HIDPP_DEVICE(self), NULL);
	return priv->model_id;
}

static void
fu_logitech_hidpp_device_set_model_id(FuLogitechHidPpDevice *self, const gchar *model_id)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_HIDPP_DEVICE(self));
	if (g_strcmp0(priv->model_id, model_id) == 0)
		return;
	g_free(priv->model_id);
	priv->model_id = g_strdup(model_id);
}

static const gchar *
fu_logitech_hidpp_device_get_icon(FuLogitechHidPpDeviceKind kind)
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
fu_logitech_hidpp_device_get_summary(FuLogitechHidPpDeviceKind kind)
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
fu_logitech_hidpp_feature_to_string(guint16 feature)
{
	if (feature == HIDPP_FEATURE_ROOT)
		return "Root";
	if (feature == HIDPP_FEATURE_I_FIRMWARE_INFO)
		return "IFirmwareInfo";
	if (feature == HIDPP_FEATURE_GET_DEVICE_NAME_TYPE)
		return "GetDevicenameType";
	if (feature == HIDPP_FEATURE_BATTERY_LEVEL_STATUS)
		return "BatteryLevelStatus";
	if (feature == HIDPP_FEATURE_UNIFIED_BATTERY)
		return "UnifiedBattery";
	if (feature == HIDPP_FEATURE_DFU_CONTROL)
		return "DfuControl";
	if (feature == HIDPP_FEATURE_DFU_CONTROL_SIGNED)
		return "DfuControlSigned";
	if (feature == HIDPP_FEATURE_DFU_CONTROL_BOLT)
		return "DfuControlBolt";
	if (feature == HIDPP_FEATURE_DFU)
		return "Dfu";
	return NULL;
}

static gboolean
fu_logitech_hidpp_device_ping(FuLogitechHidPpDevice *self, GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	gdouble version;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	GPtrArray *children = NULL;

	/* handle failure */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = 0x00;	      /* rootIndex */
	msg->function_id = 0x01 << 4; /* ping */
	msg->data[0] = 0x00;
	msg->data[1] = 0x00;
	msg->data[2] = 0xaa; /* user-selected value */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, &error_local)) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
			priv->hidpp_version = 1;
			return TRUE;
		}
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE)) {
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNREACHABLE);
			fu_device_inhibit(FU_DEVICE(self), "unreachable", "device is unreachable");
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* device no longer asleep */
	fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNREACHABLE);
	fu_device_uninhibit(FU_DEVICE(self), "unreachable");
	children = fu_device_get_children(FU_DEVICE(self));
	for (guint i = 0; i < children->len; i++) {
		FuDevice *radio = g_ptr_array_index(children, i);
		fu_device_remove_flag(radio, FWUPD_DEVICE_FLAG_UNREACHABLE);
		fu_device_uninhibit(radio, "unreachable");
	}

	/* if the device index is unset, grab it from the reply */
	if (priv->device_idx == HIDPP_DEVICE_IDX_UNSET &&
	    msg->device_id != HIDPP_DEVICE_IDX_UNSET) {
		priv->device_idx = msg->device_id;
		g_debug("Device index is %02x", priv->device_idx);
	}

	/* format version in BCD format */
	if (priv->hidpp_version != FU_HIDPP_VERSION_BLE) {
		version = (gdouble)msg->data[0] + ((gdouble)msg->data[1]) / 100.f;
		priv->hidpp_version = (guint)version;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_close(FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown(priv->io_channel, error))
			return FALSE;
		g_clear_object(&priv->io_channel);
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_poll(FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(self, error);
	if (locker == NULL)
		return FALSE;

	/* flush pending data */
	msg->device_id = priv->device_idx;
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_receive(priv->io_channel, msg, timeout, &error_local)) {
		if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
			g_warning("failed to get pending read: %s", error_local->message);
			return TRUE;
		}
		/* no data to receive */
		g_clear_error(&error_local);
	}

	/* just ping */
	if (!fu_logitech_hidpp_device_ping(self, &error_local)) {
		g_warning("failed to ping %s: %s",
			  fu_device_get_name(FU_DEVICE(self)),
			  error_local->message);
		return TRUE;
	}

	/* this is the first time the device has been active */
	if (priv->feature_index->len == 0) {
		fu_device_probe_invalidate(FU_DEVICE(self));
		if (!fu_device_setup(FU_DEVICE(self), error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_open(FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	const gchar *devpath = g_udev_device_get_device_file(udev_device);

	/* open */
	priv->io_channel = fu_io_channel_new_file(devpath, error);
	if (priv->io_channel == NULL)
		return FALSE;

	return TRUE;
}

static void
fu_logitech_hidpp_map_to_string(FuLogitechHidPpHidppMap *map, guint idt, GString *str)
{
	g_autofree gchar *title = g_strdup_printf("Feature%02x", map->idx);
	g_autofree gchar *tmp = g_strdup_printf("%s [0x%04x]",
						fu_logitech_hidpp_feature_to_string(map->feature),
						map->feature);
	fu_common_string_append_kv(str, idt, title, tmp);
}

static void
fu_logitech_hidpp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_logitech_hidpp_device_parent_class)->to_string(device, idt, str);

	fu_common_string_append_ku(str, idt, "HidppVersion", priv->hidpp_version);
	fu_common_string_append_ku(str, idt, "HidppPid", priv->hidpp_pid);
	fu_common_string_append_kx(str, idt, "DeviceIdx", priv->device_idx);
	fu_common_string_append_kv(str, idt, "ModelId", priv->model_id);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		FuLogitechHidPpHidppMap *map = g_ptr_array_index(priv->feature_index, i);
		fu_logitech_hidpp_map_to_string(map, idt, str);
	}
}

static guint8
fu_logitech_hidpp_device_feature_get_idx(FuLogitechHidPpDevice *self, guint16 feature)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);

	for (guint i = 0; i < priv->feature_index->len; i++) {
		FuLogitechHidPpHidppMap *map = g_ptr_array_index(priv->feature_index, i);
		if (map->feature == feature)
			return map->idx;
	}
	return 0x00;
}

static gboolean
fu_logitech_hidpp_device_create_radio_child(FuLogitechHidPpDevice *self,
					    guint8 entity,
					    guint16 build,
					    GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *logical_id = NULL;
	g_autofree gchar *radio_version = NULL;
	g_autoptr(FuLogitechHidPpRadio) radio = NULL;
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));

	/* sanity check */
	if (priv->model_id == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "model ID not set");
		return FALSE;
	}

	radio_version = g_strdup_printf("0x%.4x", build);
	radio = fu_logitech_hidpp_radio_new(ctx, entity);
	fu_device_set_physical_id(FU_DEVICE(radio), fu_device_get_physical_id(FU_DEVICE(self)));
	/*
	 * Use the parent logical id as well as the model id for the
	 * logical id of the radio child device. This allows the radio
	 * devices of two devices of the same type (same device type,
	 * BLE mode) to coexist correctly.
	 */
	logical_id =
	    g_strdup_printf("%s-%s", fu_device_get_logical_id(FU_DEVICE(self)), priv->model_id);
	fu_device_set_logical_id(FU_DEVICE(radio), logical_id);
	instance_id = g_strdup_printf("HIDRAW\\VEN_%04X&MOD_%s&ENT_05",
				      (guint)FU_UNIFYING_DEVICE_VID,
				      priv->model_id);
	fu_device_add_instance_id(FU_DEVICE(radio), instance_id);
	fu_device_set_version(FU_DEVICE(radio), radio_version);
	if (!fu_device_setup(FU_DEVICE(radio), error))
		return FALSE;

	/* remove old radio device if it already existed */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (g_strcmp0(fu_device_get_physical_id(FU_DEVICE(radio)),
			      fu_device_get_physical_id(child)) == 0 &&
		    g_strcmp0(fu_device_get_logical_id(FU_DEVICE(radio)),
			      fu_device_get_logical_id(child)) == 0) {
			fu_device_remove_child(FU_DEVICE(self), child);
			break;
		}
	}
	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(radio));
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_fetch_firmware_info(FuLogitechHidPpDevice *self, GError **error)
{
	guint8 idx;
	guint8 entity_count;
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	gboolean radio_ok = FALSE;

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get the entity count */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = idx;
	msg->function_id = 0x00 << 4; /* getCount */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
		g_prefix_error(error, "failed to get firmware count: ");
		return FALSE;
	}
	entity_count = msg->data[0];
	g_debug("firmware entity count is %u", entity_count);

	/* get firmware, bootloader, hardware versions */
	for (guint8 i = 0; i < entity_count; i++) {
		guint16 build;
		g_autofree gchar *version = NULL;
		g_autofree gchar *name = NULL;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* getInfo */
		msg->data[0] = i;
		if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
			g_prefix_error(error, "failed to get firmware info: ");
			return FALSE;
		}
		if (msg->data[1] == 0x00 && msg->data[2] == 0x00 && msg->data[3] == 0x00 &&
		    msg->data[4] == 0x00 && msg->data[5] == 0x00 && msg->data[6] == 0x00 &&
		    msg->data[7] == 0x00) {
			g_debug("no version set for entity %u", i);
			continue;
		}
		name = g_strdup_printf("%c%c%c", msg->data[1], msg->data[2], msg->data[3]);
		build = ((guint16)msg->data[6]) << 8 | msg->data[7];
		version = fu_logitech_hidpp_format_version(name, msg->data[4], msg->data[5], build);
		g_debug("firmware entity 0x%02x version is %s", i, version);
		if (msg->data[0] == 0) {
			fu_device_set_version(FU_DEVICE(self), version);
			priv->cached_fw_entity = i;
		} else if (msg->data[0] == 1) {
			fu_device_set_version_bootloader(FU_DEVICE(self), version);
		} else if (msg->data[0] == 2) {
			fu_device_set_metadata(FU_DEVICE(self), "version-hw", version);
		} else if (msg->data[0] == 5 &&
			   fu_device_has_private_flag(FU_DEVICE(self),
						      FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO)) {
			if (!fu_logitech_hidpp_device_create_radio_child(self, i, build, error)) {
				g_prefix_error(error, "failed to create radio: ");
				return FALSE;
			}
			radio_ok = TRUE;
		}
	}

	/* the device is probably in bootloader mode and the last SoftDevice FW upgrade failed */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO) &&
	    !radio_ok) {
		g_debug("no radio found, creating a fake one for recovery");
		if (!fu_logitech_hidpp_device_create_radio_child(self, 1, 0, error)) {
			g_prefix_error(error, "failed to create radio: ");
			return FALSE;
		}
	}

	/* not an error, the device just doesn't support this */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_fetch_model_id(FuLogitechHidPpDevice *self, GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	g_autofree gchar *devid = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GString) str = g_string_new(NULL);

	/* get the (optional) feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = idx;
	msg->function_id = 0x00 << 4; /* getDeviceInfo */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
		g_prefix_error(error, "failed to get the model ID: ");
		return FALSE;
	}

	/* ignore extendedModelID in data[13] */
	for (guint i = 7; i < 13; i++)
		g_string_append_printf(str, "%02X", msg->data[i]);
	fu_logitech_hidpp_device_set_model_id(self, str->str);

	/* add one more instance ID */
	devid = g_strdup_printf("HIDRAW\\VEN_%04X&MOD_%s",
				(guint)FU_UNIFYING_DEVICE_VID,
				priv->model_id);
	fu_device_add_instance_id(FU_DEVICE(self), devid);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_fetch_battery_level(FuLogitechHidPpDevice *self, GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);

	/* try using HID++2.0 */
	if (priv->hidpp_version >= 2.f) {
		guint8 idx;

		/* try the Unified Battery feature first */
		idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_UNIFIED_BATTERY);
		if (idx != 0x00) {
			gboolean socc = FALSE; /* state of charge capability */
			g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
			msg->report_id = HIDPP_REPORT_ID_SHORT;
			msg->device_id = priv->device_idx;
			msg->sub_id = idx;
			msg->function_id = 0x00 << 4; /* get_capabilities */
			msg->hidpp_version = priv->hidpp_version;
			if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
				g_prefix_error(error, "failed to get battery info: ");
				return FALSE;
			}
			if (msg->data[1] & 0x02)
				socc = TRUE;
			msg->function_id = 0x01 << 4; /* get_status */
			if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
				g_prefix_error(error, "failed to get battery info: ");
				return FALSE;
			}
			if (socc) {
				fu_device_set_battery_level(FU_DEVICE(self), msg->data[0]);
			} else {
				switch (msg->data[1]) {
				case 1: /* critical */
					fu_device_set_battery_level(FU_DEVICE(self), 5);
					break;
				case 2: /* low */
					fu_device_set_battery_level(FU_DEVICE(self), 20);
					break;
				case 4: /* good */
					fu_device_set_battery_level(FU_DEVICE(self), 55);
					break;
				case 8: /* full */
					fu_device_set_battery_level(FU_DEVICE(self), 90);
					break;
				default:
					g_warning("unknown battery level: 0x%02x", msg->data[1]);
					break;
				}
			}
			return TRUE;
		} else {
			/* fall back to the legacy Battery Level feature */
			idx = fu_logitech_hidpp_device_feature_get_idx(
			    self,
			    HIDPP_FEATURE_BATTERY_LEVEL_STATUS);
			if (idx != 0x00) {
				g_autoptr(FuLogitechHidPpHidppMsg) msg =
				    fu_logitech_hidpp_msg_new();
				msg->report_id = HIDPP_REPORT_ID_SHORT;
				msg->device_id = priv->device_idx;
				msg->sub_id = idx;
				msg->function_id = 0x00 << 4; /* GetBatteryLevelStatus */
				msg->hidpp_version = priv->hidpp_version;
				if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
					g_prefix_error(error, "failed to get battery info: ");
					return FALSE;
				}
				if (msg->data[0] != 0x00)
					fu_device_set_battery_level(FU_DEVICE(self), msg->data[0]);
				return TRUE;
			}
		}
	}

	/* try HID++1.0 battery mileage */
	if (priv->hidpp_version == 1.f) {
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = HIDPP_SUBID_GET_REGISTER;
		msg->function_id = HIDPP_REGISTER_BATTERY_MILEAGE << 4;
		msg->hidpp_version = priv->hidpp_version;
		if (fu_logitech_hidpp_transfer(priv->io_channel, msg, NULL)) {
			if (msg->data[0] != 0x7F)
				fu_device_set_battery_level(FU_DEVICE(self), msg->data[0]);
			else
				g_warning("unknown battery level: 0x%02x", msg->data[0]);
			return TRUE;
		}

		/* try HID++1.0 battery status instead */
		msg->function_id = HIDPP_REGISTER_BATTERY_STATUS << 4;
		if (fu_logitech_hidpp_transfer(priv->io_channel, msg, NULL)) {
			switch (msg->data[0]) {
			case 1: /* 0 - 10 */
				fu_device_set_battery_level(FU_DEVICE(self), 5);
				break;
			case 3: /* 11 - 30 */
				fu_device_set_battery_level(FU_DEVICE(self), 20);
				break;
			case 5: /* 31 - 80 */
				fu_device_set_battery_level(FU_DEVICE(self), 55);
				break;
			case 7: /* 81 - 100 */
				fu_device_set_battery_level(FU_DEVICE(self), 90);
				break;
			default:
				g_warning("unknown battery percentage: 0x%02x", msg->data[0]);
				break;
			}
			return TRUE;
		}
	}

	/* not an error, the device just doesn't support any of the methods */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_feature_search(FuDevice *device, guint16 feature, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	FuLogitechHidPpHidppMap *map;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();

	/* find the idx for the feature */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = 0x00;	      /* rootIndex */
	msg->function_id = 0x00 << 4; /* getFeature */
	msg->data[0] = feature >> 8;
	msg->data[1] = feature;
	msg->data[2] = 0x00;
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
		g_prefix_error(error,
			       "failed to get idx for feature %s [0x%04x]: ",
			       fu_logitech_hidpp_feature_to_string(feature),
			       feature);
		return FALSE;
	}

	/* zero index */
	if (msg->data[0] == 0x00) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "feature %s [0x%04x] not found",
			    fu_logitech_hidpp_feature_to_string(feature),
			    feature);
		return FALSE;
	}

	/* add to map */
	map = g_new0(FuLogitechHidPpHidppMap, 1);
	map->idx = msg->data[0];
	map->feature = feature;
	g_ptr_array_add(priv->feature_index, map);
	g_debug("added feature %s [0x%04x] as idx %02x",
		fu_logitech_hidpp_feature_to_string(feature),
		feature,
		map->idx);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_probe(FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);

	/*
	 * FuUdevDevice->probe except for paired devices. We don't want
	 * paired devices to inherit the logical ids of the receiver.
	 */
	if (priv->device_idx == HIDPP_DEVICE_IDX_UNSET ||
	    priv->device_idx == HIDPP_DEVICE_IDX_BLE) {
		if (!FU_DEVICE_CLASS(fu_logitech_hidpp_device_parent_class)->probe(device, error))
			return FALSE;
	}

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error))
		return FALSE;

	/* nearly... */
	fu_device_add_vendor_id(device, "USB:0x046D");

	/*
	 * All devices connected to a Bolt receiver share the same
	 * physical id, make them unique by using their pairing slot
	 * (device index) as a basis for their logical id.
	 */
	if (priv->device_idx != HIDPP_DEVICE_IDX_UNSET &&
	    priv->device_idx != HIDPP_DEVICE_IDX_BLE) {
		g_autoptr(GString) id_str = g_string_new(NULL);
		g_string_append_printf(id_str, "DEV_IDX=%d", priv->device_idx);
		fu_device_set_logical_id(device, id_str->str);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_setup(FuDevice *device, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	const guint16 map_features[] = {HIDPP_FEATURE_GET_DEVICE_NAME_TYPE,
					HIDPP_FEATURE_I_FIRMWARE_INFO,
					HIDPP_FEATURE_BATTERY_LEVEL_STATUS,
					HIDPP_FEATURE_UNIFIED_BATTERY,
					HIDPP_FEATURE_DFU_CONTROL,
					HIDPP_FEATURE_DFU_CONTROL_SIGNED,
					HIDPP_FEATURE_DFU_CONTROL_BOLT,
					HIDPP_FEATURE_DFU,
					HIDPP_FEATURE_ROOT};

	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE)) {
		priv->hidpp_version = FU_HIDPP_VERSION_BLE;
		priv->device_idx = HIDPP_DEVICE_IDX_BLE;
		/*
		 * Set the logical ID for BLE devices. Note that for BLE
		 * devices, physical_id = HID_PHYS = MAC of the BT adapter,
		 * logical_id = HID_UNIQ = MAC of the device. The physical id is
		 * not enough to differentiate two BLE devices connected to the
		 * same adapter. This is done here because private flags
		 * are not loaded when the probe method runs, so we
		 * can't tell the device is in BLE mode.
		 */
		if (!fu_udev_device_set_logical_id(FU_UDEV_DEVICE(device), "hid", error))
			return FALSE;
		/*
		 * BLE devices might not be ready for ping right after
		 * they come up -> wait a bit before pinging.
		 */
		g_usleep(G_USEC_PER_SEC);
	}
	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID))
		priv->device_idx = HIDPP_DEVICE_IDX_RECEIVER;

	/* ping device to get HID++ version */
	if (!fu_logitech_hidpp_device_ping(self, error))
		return FALSE;

	/* did not get ID */
	if (priv->device_idx == HIDPP_DEVICE_IDX_UNSET) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no HID++ ID");
		return FALSE;
	}

	/* add known root for HID++2.0 */
	g_ptr_array_set_size(priv->feature_index, 0);
	if (priv->hidpp_version >= 2.f) {
		FuLogitechHidPpHidppMap *map = g_new0(FuLogitechHidPpHidppMap, 1);
		map->idx = 0x00;
		map->feature = HIDPP_FEATURE_ROOT;
		g_ptr_array_add(priv->feature_index, map);
	}

	/* map some *optional* HID++2.0 features we might use */
	for (guint i = 0; map_features[i] != HIDPP_FEATURE_ROOT; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_logitech_hidpp_feature_search(device, map_features[i], &error_local)) {
			g_debug("%s", error_local->message);
			if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
			    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE)) {
				/* timed out, so not trying any more */
				break;
			}
		}
	}

	/* get the model ID, typically something like B3630000000000 */
	if (!fu_logitech_hidpp_device_fetch_model_id(self, error))
		return FALSE;

	/* get the firmware information */
	if (!fu_logitech_hidpp_device_fetch_firmware_info(self, error))
		return FALSE;

	/* get the battery level */
	if (!fu_logitech_hidpp_device_fetch_battery_level(self, error))
		return FALSE;

	/* try using HID++2.0 */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_GET_DEVICE_NAME_TYPE);
	if (idx != 0x00) {
		const gchar *tmp;
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x02 << 4; /* getDeviceType */
		msg->hidpp_version = priv->hidpp_version;
		if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
			g_prefix_error(error, "failed to get device type: ");
			return FALSE;
		}

		/* add nice-to-have data */
		tmp = fu_logitech_hidpp_device_get_summary(msg->data[0]);
		if (tmp != NULL)
			fu_device_set_summary(FU_DEVICE(device), tmp);
		tmp = fu_logitech_hidpp_device_get_icon(msg->data[0]);
		if (tmp != NULL)
			fu_device_add_icon(FU_DEVICE(device), tmp);
	}
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_remove_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifying");
	}
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU_CONTROL_BOLT);
	if (idx == 0x00)
		idx = fu_logitech_hidpp_device_feature_get_idx(self,
							       HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		/* check the feature is available */
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x00 << 4; /* getDfuStatus */
		msg->hidpp_version = priv->hidpp_version;
		if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
			g_prefix_error(error, "failed to get DFU status: ");
			return FALSE;
		}
		if ((msg->data[2] & 0x01) > 0) {
			g_warning("DFU mode not available");
		} else {
			fu_device_remove_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		}
		fu_device_add_protocol(FU_DEVICE(device), "com.logitech.unifyingsigned");
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	}
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU);
	if (idx != 0x00) {
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		if (fu_device_get_version(device) == NULL) {
			g_debug("repairing device in bootloader mode");
			fu_device_set_version(FU_DEVICE(device), "MPK00.00_B0000");
		}
		/* we do not actually know which protocol when in recovery mode,
		 * so force the metadata to have the specific regex set up */
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifying");
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
	}

	/* poll for pings to track active state */
	fu_device_set_poll_interval(device, FU_HIDPP_DEVICE_POLLING_INTERVAL);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* these may require user action */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU_CONTROL_BOLT);
	if (idx == 0x00)
		idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		FuDevice *parent;
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		g_autoptr(GError) error_local = NULL;
		msg->report_id = HIDPP_REPORT_ID_LONG;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* setDfuControl */
		msg->data[0] = 0x01;	      /* enterDfu */
		msg->data[1] = 0x00;	      /* dfuControlParam */
		msg->data[2] = 0x00;	      /* unused */
		msg->data[3] = 0x00;	      /* unused */
		msg->data[4] = 'D';
		msg->data[5] = 'F';
		msg->data[6] = 'U';
		msg->hidpp_version = priv->hidpp_version;
		msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
			     FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
		if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, &error_local)) {
			if (fu_device_has_private_flag(
				device,
				FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED)) {
				g_debug("ignoring %s", error_local->message);
				fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
				return TRUE;
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to put device into DFU mode: ");
			return FALSE;
		}
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

		/* so we detect off then on */
		parent = fu_device_get_parent(device);
		if (parent != NULL)
			fu_device_set_poll_interval(parent, 500);

		/* generate a message if not already set */
		if (!fu_device_has_private_flag(
			device,
			FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED)) {
			if (fu_device_get_update_message(device) == NULL) {
				g_autofree gchar *str = NULL;
				str = g_strdup_printf(
				    "%s needs to be manually restarted to complete the update. "
				    "Please turn it off and on.",
				    fu_device_get_name(device));
				fu_device_set_update_message(device, str);
			}
			fwupd_request_set_message(request, fu_device_get_update_message(device));
			fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
			fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
			fu_device_emit_request(device, request);
		}
		return TRUE;
	}

	/* this can reboot all by itself */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		msg->report_id = HIDPP_REPORT_ID_LONG;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* setDfuControl */
		msg->data[0] = 0x01;	      /* startDfu */
		msg->data[1] = 0x00;	      /* dfuControlParam */
		msg->data[2] = 0x00;	      /* unused */
		msg->data[3] = 0x00;	      /* unused */
		msg->data[4] = 'D';
		msg->data[5] = 'F';
		msg->data[6] = 'U';
		msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID;
		if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
			g_prefix_error(error, "failed to put device into DFU mode: ");
			return FALSE;
		}
		g_usleep(200 * 1000);
		return fu_logitech_hidpp_device_setup(FU_DEVICE(self), error);
	}

	/* we don't know how */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "no method to detach");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_check_status(guint8 status, GError **error)
{
	switch (status & 0x7f) {
	case 0x00:
		g_set_error(error,
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
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_PENDING,
				    "wait for event (command in progress)");
		break;
	case 0x04:
	case 0x10: /* unknown */
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "generic error");
		break;
	case 0x11:
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "bad voltage (power too low?)");
		break;
	case 0x12:
	case 0x14: /* bad magic string */
	case 0x21: /* bad firmware */
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "unsupported firmware");
		break;
	case 0x13:
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "unsupported encryption mode");
		break;
	case 0x15:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "erase failure");
		break;
	case 0x16:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "DFU not started");
		break;
	case 0x17:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "bad sequence number");
		break;
	case 0x18:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "unsupported command");
		break;
	case 0x19:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "command in progress");
		break;
	case 0x1a:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "address out of range");
		break;
	case 0x1b:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "unaligned address");
		break;
	case 0x1c:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "bad size");
		break;
	case 0x1d:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "missing program data");
		break;
	case 0x1e:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "missing check data");
		break;
	case 0x1f:
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "program failed to write");
		break;
	case 0x20:
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "program failed to verify");
		break;
	case 0x22:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "firmware check failure");
		break;
	case 0x23:
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "blocked command (restart required)");
		break;
	default:
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "unhandled status value 0x%02x",
			    status);
		break;
	}
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_pkt(FuLogitechHidPpDevice *self,
					    guint8 idx,
					    guint8 cmd,
					    const guint8 *data,
					    GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	guint32 packet_cnt;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;

	/* send firmware data */
	msg->report_id = HIDPP_REPORT_ID_LONG;
	msg->device_id = priv->device_idx;
	msg->sub_id = idx;
	msg->function_id = cmd << 4; /* dfuStart or dfuCmdDataX */
	msg->hidpp_version = priv->hidpp_version;
	/* enable transfer workaround for devices paired to Bolt receiver */
	if (priv->device_idx != HIDPP_DEVICE_IDX_UNSET && priv->device_idx != HIDPP_DEVICE_IDX_BLE)
		msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_RETRY_STUCK;
	memcpy(msg->data, data, 16);
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
		g_prefix_error(error, "failed to supply program data: ");
		return FALSE;
	}

	/* check error */
	if (!fu_common_read_uint32_safe(msg->data,
					sizeof(msg->data),
					0x0,
					&packet_cnt,
					G_BIG_ENDIAN,
					error))
		return FALSE;
	if (g_getenv("FWUPD_LOGITECH_HIDPP_VERBOSE") != NULL)
		g_debug("packet_cnt=0x%04x", packet_cnt);
	if (fu_logitech_hidpp_device_check_status(msg->data[4], &error_local))
		return TRUE;

	/* fatal error */
	if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_PENDING)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, error_local->message);
		return FALSE;
	}

	/* wait for the HID++ notification */
	g_debug("ignoring: %s", error_local->message);
	for (guint retry = 0; retry < 10; retry++) {
		g_autoptr(FuLogitechHidPpHidppMsg) msg2 = fu_logitech_hidpp_msg_new();
		msg2->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID;
		if (!fu_logitech_hidpp_receive(priv->io_channel, msg2, 15000, error))
			return FALSE;
		if (fu_logitech_hidpp_msg_is_reply(msg, msg2)) {
			g_autoptr(GError) error2 = NULL;
			if (!fu_logitech_hidpp_device_check_status(msg2->data[4], &error2)) {
				g_debug("got %s, waiting a bit longer", error2->message);
				continue;
			}
			return TRUE;
		} else {
			g_debug("got wrong packet, continue to wait...");
		}
	}

	/* nothing in the queue */
	g_set_error_literal(error,
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
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	gsize sz = 0;
	const guint8 *data;
	guint8 cmd = 0x04;
	guint8 idx;
	g_autoptr(GBytes) fw = NULL;

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "no DFU feature available");
		return FALSE;
	}

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* flash hardware -- the first data byte is the fw entity */
	data = g_bytes_get_data(fw, &sz);
	if (priv->cached_fw_entity != data[0]) {
		g_warning("updating cached entity 0x%x with 0x%x", priv->cached_fw_entity, data[0]);
		priv->cached_fw_entity = data[0];
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (gsize i = 0; i < sz / 16; i++) {
		/* send packet and wait for reply */
		if (g_getenv("FWUPD_LOGITECH_HIDPP_VERBOSE") != NULL)
			g_debug("send data at addr=0x%04x", (guint)i * 16);
		if (!fu_logitech_hidpp_device_write_firmware_pkt(self,
								 idx,
								 cmd,
								 data + (i * 16),
								 error)) {
			g_prefix_error(error, "failed to write @0x%04x: ", (guint)i * 16);
			return FALSE;
		}

		/* use sliding window */
		cmd = (cmd + 1) % 4;

		/* update progress-bar */
		fu_progress_set_percentage_full(progress, (i + 1) * 16, sz);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_reprobe_cb(FuDevice *device, gpointer user_data, GError **error)
{
	return fu_logitech_hidpp_device_setup(device, error);
}

gboolean
fu_logitech_hidpp_device_attach(FuLogitechHidPpDevice *self,
				guint8 entity,
				FuProgress *progress,
				GError **error)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	FuDevice *device = FU_DEVICE(self);
	guint8 idx;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "no DFU feature available");
		return FALSE;
	}

	/* reboot back into firmware mode */
	msg->report_id = HIDPP_REPORT_ID_LONG;
	msg->device_id = priv->device_idx;
	msg->sub_id = idx;
	msg->function_id = 0x05 << 4; /* restart */
	msg->data[0] = entity;	      /* fwEntity */
	msg->hidpp_version = priv->hidpp_version;
	msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
		     FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SWID | // inferred?
		     FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring '%s' on reset", error_local->message);
		} else {
			g_prefix_error(&error_local, "failed to restart device: ");
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH)) {
		fu_device_set_poll_interval(device, 0);
		/*
		 * Wait for device to become ready after flashing.
		 * Possible race condition: after the device is reset, Linux might enumerate it as
		 * a different hidraw device depending on timing.
		 */
		fu_progress_sleep(progress, 1000); /* ms */
	} else {
		/* device file hasn't been unbound/re-bound, just probe again */
		if (!fu_device_retry(device, fu_logitech_hidpp_device_reprobe_cb, 10, NULL, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_attach_cached(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);

	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return fu_logitech_hidpp_device_attach(self, priv->cached_fw_entity, progress, error);
}

static gboolean
fu_logitech_hidpp_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(device);
	if (g_strcmp0(key, "LogitechHidppModelId") == 0) {
		fu_logitech_hidpp_device_set_model_id(self, value);
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_logitech_hidpp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_logitech_hidpp_device_finalize(GObject *object)
{
	FuLogitechHidPpDevice *self = FU_HIDPP_DEVICE(object);
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->feature_index);
	g_free(priv->model_id);
	G_OBJECT_CLASS(fu_logitech_hidpp_device_parent_class)->finalize(object);
}

static gboolean
fu_logitech_hidpp_device_cleanup(FuDevice *device, FwupdInstallFlags flags, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	if (parent != NULL)
		fu_device_set_poll_interval(parent, FU_HIDPP_RECEIVER_RUNTIME_POLLING_INTERVAL);

	return TRUE;
}

static void
fu_logitech_hidpp_device_class_init(FuLogitechHidPpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logitech_hidpp_device_finalize;
	klass_device->setup = fu_logitech_hidpp_device_setup;
	klass_device->open = fu_logitech_hidpp_device_open;
	klass_device->close = fu_logitech_hidpp_device_close;
	klass_device->write_firmware = fu_logitech_hidpp_device_write_firmware;
	klass_device->attach = fu_logitech_hidpp_device_attach_cached;
	klass_device->detach = fu_logitech_hidpp_device_detach;
	klass_device->poll = fu_logitech_hidpp_device_poll;
	klass_device->to_string = fu_logitech_hidpp_device_to_string;
	klass_device->probe = fu_logitech_hidpp_device_probe;
	klass_device->set_quirk_kv = fu_logitech_hidpp_device_set_quirk_kv;
	klass_device->cleanup = fu_logitech_hidpp_device_cleanup;
	klass_device->set_progress = fu_logitech_hidpp_device_set_progress;
}

static void
fu_logitech_hidpp_device_init(FuLogitechHidPpDevice *self)
{
	FuLogitechHidPpDevicePrivate *priv = GET_PRIVATE(self);
	priv->device_idx = HIDPP_DEVICE_IDX_UNSET;
	priv->feature_index = g_ptr_array_new_with_free_func(g_free);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_vendor(FU_DEVICE(self), "Logitech");
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID,
					"force-receiver-id");
	fu_device_register_private_flag(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE, "ble");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH,
					"rebind-attach");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED,
					"no-request-required");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO,
					"add-radio");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_battery_threshold(FU_DEVICE(self), 20);
}

FuLogitechHidPpDevice *
fu_logitech_hidpp_device_new(FuUdevDevice *parent)
{
	FuLogitechHidPpDevice *self = NULL;
	FuLogitechHidPpDevicePrivate *priv;
	self = g_object_new(FU_TYPE_HIDPP_DEVICE,
			    "context",
			    fu_device_get_context(FU_DEVICE(parent)),
			    "physical-id",
			    fu_device_get_physical_id(FU_DEVICE(parent)),
			    "udev-device",
			    fu_udev_device_get_dev(parent),
			    NULL);
	priv = GET_PRIVATE(self);
	priv->io_channel = fu_logitech_hidpp_runtime_get_io_channel(FU_HIDPP_RUNTIME(parent));
	return self;
}
