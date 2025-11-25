/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-radio.h"
#include "fu-logitech-hidpp-rdfu-struct.h"
#include "fu-logitech-hidpp-runtime-bolt.h"
#include "fu-logitech-hidpp-struct.h"
#include "fu-logitech-rdfu-firmware.h"

typedef struct {
	guint8 cached_fw_entity;
	/*
	 * Device index:
	 *   - FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER for the receiver or BLE devices
	 *   - pairing slot for paired Bolt devices.
	 */
	guint8 device_idx;
	guint16 hidpp_pid;
	guint8 hidpp_version;
	gchar *model_id;
	GPtrArray *feature_index; /* of FuLogitechHidppHidppMap */
	FuLogitechHidppRdfuState rdfu_state;
	guint8 rdfu_capabilities;
	guint16 rdfu_block;
	guint32 rdfu_pkt;
	guint32 rdfu_wait;
} FuLogitechHidppDevicePrivate;

typedef struct {
	guint8 idx;
	guint16 feature;
} FuLogitechHidppHidppMap;

#define FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG 16
/* max attempts to resume after non-critical errors */
#define FU_LOGITECH_HIDPP_DEVICE_RDFU_MAX_RETRIES 10

G_DEFINE_TYPE_WITH_PRIVATE(FuLogitechHidppDevice, fu_logitech_hidpp_device, FU_TYPE_UDEV_DEVICE)

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
} FuLogitechHidppDeviceKind;

void
fu_logitech_hidpp_device_set_device_idx(FuLogitechHidppDevice *self, guint8 device_idx)
{
	FuLogitechHidppDevicePrivate *priv;
	g_return_if_fail(FU_IS_HIDPP_DEVICE(self));
	priv = GET_PRIVATE(self);
	priv->device_idx = device_idx;
}

guint16
fu_logitech_hidpp_device_get_hidpp_pid(FuLogitechHidppDevice *self)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_HIDPP_DEVICE(self), G_MAXUINT16);
	return priv->hidpp_pid;
}

void
fu_logitech_hidpp_device_set_hidpp_pid(FuLogitechHidppDevice *self, guint16 hidpp_pid)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_HIDPP_DEVICE(self));
	priv->hidpp_pid = hidpp_pid;
}

static void
fu_logitech_hidpp_device_set_model_id(FuLogitechHidppDevice *self, const gchar *model_id)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_HIDPP_DEVICE(self));
	if (g_strcmp0(priv->model_id, model_id) == 0)
		return;
	g_free(priv->model_id);
	priv->model_id = g_strdup(model_id);
}

static const gchar *
fu_logitech_hidpp_device_get_icon(FuLogitechHidppDeviceKind kind)
{
	if (kind == FU_HIDPP_DEVICE_KIND_KEYBOARD)
		return FU_DEVICE_ICON_INPUT_KEYBOARD;
	if (kind == FU_HIDPP_DEVICE_KIND_REMOTE_CONTROL)
		return FU_DEVICE_ICON_PDA; // ish
	if (kind == FU_HIDPP_DEVICE_KIND_NUMPAD)
		return FU_DEVICE_ICON_INPUT_DIALPAD;
	if (kind == FU_HIDPP_DEVICE_KIND_MOUSE)
		return FU_DEVICE_ICON_INPUT_MOUSE;
	if (kind == FU_HIDPP_DEVICE_KIND_TOUCHPAD)
		return FU_DEVICE_ICON_INPUT_TOUCHPAD;
	if (kind == FU_HIDPP_DEVICE_KIND_TRACKBALL)
		return FU_DEVICE_ICON_INPUT_MOUSE; // ish
	if (kind == FU_HIDPP_DEVICE_KIND_PRESENTER)
		return FU_DEVICE_ICON_PDA; // ish
	if (kind == FU_HIDPP_DEVICE_KIND_RECEIVER)
		return FU_DEVICE_ICON_USB_RECEIVER;
	return NULL;
}

static const gchar *
fu_logitech_hidpp_device_get_summary(FuLogitechHidppDeviceKind kind)
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

static gboolean
fu_logitech_hidpp_device_ping(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	GPtrArray *children = NULL;

	/* handle failure */
	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = 0x00;	      /* rootIndex */
	msg->function_id = 0x01 << 4; /* ping */
	msg->data[0] = 0x00;
	msg->data[1] = 0x00;
	msg->data[2] = 0xaa; /* user-selected value */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			priv->hidpp_version = 1;
			return TRUE;
		}
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNREACHABLE);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* device no longer asleep */
	fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNREACHABLE);
	children = fu_device_get_children(FU_DEVICE(self));
	for (guint i = 0; i < children->len; i++) {
		FuDevice *radio = g_ptr_array_index(children, i);
		fu_device_remove_flag(radio, FWUPD_DEVICE_FLAG_UNREACHABLE);
	}

	/* if the device index is unset, grab it from the reply */
	if (priv->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    msg->device_id != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED) {
		priv->device_idx = msg->device_id;
		g_debug("device index is %02x", priv->device_idx);
	}

	/* format version in BCD format */
	if (priv->hidpp_version != FU_HIDPP_VERSION_BLE) {
		/* minor version is in msg->data[1] */;
		priv->hidpp_version = msg->data[0];
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_poll(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(self, error);
	if (locker == NULL)
		return FALSE;

	/* flush pending data */
	msg->device_id = priv->device_idx;
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_receive(FU_UDEV_DEVICE(self), msg, timeout, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_warning("failed to get pending read: %s", error_local->message);
			return TRUE;
		}
		/* no data to receive */
		g_clear_error(&error_local);
	}

	/* just ping */
	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH)) {
		if (!fu_logitech_hidpp_device_ping(self, &error_local)) {
			g_warning("failed to ping %s: %s",
				  fu_device_get_name(device),
				  error_local->message);
			return TRUE;
		}
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

static void
fu_logitech_hidpp_device_map_to_string(FuLogitechHidppHidppMap *map, guint idt, GString *str)
{
	g_autofree gchar *title = g_strdup_printf("Feature%02x", map->idx);
	g_autofree gchar *tmp = g_strdup_printf("%s [0x%04x]",
						fu_logitech_hidpp_feature_to_string(map->feature),
						map->feature);
	fwupd_codec_string_append(str, idt, title, tmp);
}

static void
fu_logitech_hidpp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_int(str, idt, "HidppVersion", priv->hidpp_version);
	fwupd_codec_string_append_int(str, idt, "HidppPid", priv->hidpp_pid);
	fwupd_codec_string_append_hex(str, idt, "DeviceIdx", priv->device_idx);
	fwupd_codec_string_append(str, idt, "ModelId", priv->model_id);
	for (guint i = 0; i < priv->feature_index->len; i++) {
		FuLogitechHidppHidppMap *map = g_ptr_array_index(priv->feature_index, i);
		fu_logitech_hidpp_device_map_to_string(map, idt, str);
	}
}

static guint8
fu_logitech_hidpp_device_feature_get_idx(FuLogitechHidppDevice *self, guint16 feature)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);

	for (guint i = 0; i < priv->feature_index->len; i++) {
		FuLogitechHidppHidppMap *map = g_ptr_array_index(priv->feature_index, i);
		if (map->feature == feature)
			return map->idx;
	}
	return 0x00;
}

static gboolean
fu_logitech_hidpp_device_create_radio_child(FuLogitechHidppDevice *self,
					    guint8 entity,
					    guint16 build,
					    GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *logical_id = NULL;
	g_autofree gchar *radio_version = NULL;
	g_autoptr(FuLogitechHidppRadio) radio = NULL;
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));

	/* sanity check */
	if (priv->model_id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "model ID not set");
		return FALSE;
	}

	radio_version = g_strdup_printf("0x%.4x", build);
	radio = fu_logitech_hidpp_radio_new(ctx, entity);
	fu_device_incorporate(FU_DEVICE(radio),
			      FU_DEVICE(self),
			      FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
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
				      fu_device_get_vid(FU_DEVICE(self)),
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
fu_logitech_hidpp_device_fetch_firmware_info(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	guint8 entity_count;
	g_autoptr(FuLogitechHidppHidppMsg) msg_count = fu_logitech_hidpp_msg_new();
	gboolean radio_ok = FALSE;
	gboolean app_ok = FALSE;

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get the entity count */
	msg_count->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg_count->device_id = priv->device_idx;
	msg_count->sub_id = idx;
	msg_count->function_id = 0x00 << 4; /* getCount */
	msg_count->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg_count, error)) {
		g_prefix_error(error, "failed to get firmware count: ");
		return FALSE;
	}
	entity_count = msg_count->data[0];
	g_debug("firmware entity count is %u", entity_count);

	/* get firmware, bootloader, hardware versions */
	for (guint8 i = 0; i < entity_count; i++) {
		guint16 build;
		g_autofree gchar *version = NULL;
		g_autofree gchar *name = NULL;
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();

		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* getInfo */
		msg->hidpp_version = priv->hidpp_version;
		msg->data[0] = i;
		if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
			g_prefix_error(error, "failed to get firmware info: ");
			return FALSE;
		}
		/* use the single available slot, otherwise -- the slot which is not active */
		if (msg->data[0] == 0) {
			if (!app_ok || FU_BIT_IS_CLEAR(msg->data[8], 0))
				priv->cached_fw_entity = i;
			app_ok = TRUE;
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
			/* set version from the active entity */
			if (FU_BIT_IS_SET(msg->data[8], 0))
				fu_device_set_version(FU_DEVICE(self), version);
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
	    !fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU) &&
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
fu_logitech_hidpp_device_fetch_model_id(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	guint64 pid_tmp = 0;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GString) str = g_string_new(NULL);

	/* get the (optional) feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = idx;
	msg->function_id = 0x00 << 4; /* getDeviceInfo */
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
		g_prefix_error(error, "failed to get the model ID: ");
		return FALSE;
	}

	/* ignore extendedModelID in data[13] */
	for (guint i = 7; i < 13; i++)
		g_string_append_printf(str, "%02X", msg->data[i]);
	fu_logitech_hidpp_device_set_model_id(self, str->str);

	/* truncate to 4 chars and convert to a PID */
	g_string_set_size(str, 4);
	if (!fu_strtoull(str->str, &pid_tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_16, error)) {
		g_prefix_error(error, "failed to parse the model ID: ");
		return FALSE;
	}
	fu_device_set_pid(FU_DEVICE(self), (guint32)pid_tmp);

	/* add one more instance ID */
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", fu_device_get_vid(FU_DEVICE(self)));
	fu_device_add_instance_str(FU_DEVICE(self), "MOD", priv->model_id);
	return fu_device_build_instance_id(FU_DEVICE(self), error, "HIDRAW", "VEN", "MOD", NULL);
}

static gboolean
fu_logitech_hidpp_device_fetch_battery_level(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);

	/* try using HID++2.0 */
	if (priv->hidpp_version >= 2.f) {
		guint8 idx;

		/* try the Unified Battery feature first */
		idx = fu_logitech_hidpp_device_feature_get_idx(
		    self,
		    FU_LOGITECH_HIDPP_FEATURE_UNIFIED_BATTERY);
		if (idx != 0x00) {
			gboolean socc = FALSE; /* state of charge capability */
			g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
			msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
			msg->device_id = priv->device_idx;
			msg->sub_id = idx;
			msg->function_id = 0x00 << 4; /* get_capabilities */
			msg->hidpp_version = priv->hidpp_version;
			if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
				g_prefix_error(error, "failed to get battery info: ");
				return FALSE;
			}
			if (msg->data[1] & 0x02)
				socc = TRUE;
			msg->function_id = 0x01 << 4; /* get_status */
			if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
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
		}

		/* fall back to the legacy Battery Level feature */
		idx = fu_logitech_hidpp_device_feature_get_idx(
		    self,
		    FU_LOGITECH_HIDPP_FEATURE_BATTERY_LEVEL_STATUS);
		if (idx != 0x00) {
			g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
			msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
			msg->device_id = priv->device_idx;
			msg->sub_id = idx;
			msg->function_id = 0x00 << 4; /* GetBatteryLevelStatus */
			msg->hidpp_version = priv->hidpp_version;
			if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
				g_prefix_error(error, "failed to get battery info: ");
				return FALSE;
			}
			if (msg->data[0] != 0x00)
				fu_device_set_battery_level(FU_DEVICE(self), msg->data[0]);
			return TRUE;
		}
	}

	/* try HID++1.0 battery mileage */
	if (priv->hidpp_version == 1) {
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = FU_LOGITECH_HIDPP_SUBID_GET_REGISTER;
		msg->function_id = FU_LOGITECH_HIDPP_REGISTER_BATTERY_MILEAGE << 4;
		msg->hidpp_version = priv->hidpp_version;
		if (fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, NULL)) {
			if (msg->data[0] != 0x7F)
				fu_device_set_battery_level(FU_DEVICE(self), msg->data[0]);
			else
				g_warning("unknown battery level: 0x%02x", msg->data[0]);
			return TRUE;
		}

		/* try HID++1.0 battery status instead */
		msg->function_id = FU_LOGITECH_HIDPP_REGISTER_BATTERY_STATUS << 4;
		if (fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, NULL)) {
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

/* wrapper function to reuse the pre-rustgen communication */
static gboolean
fu_logitech_hidpp_device_transfer_msg(FuLogitechHidppDevice *self, GByteArray *msg, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	FuLogitechHidppHidppMsg *hidpp_msg = NULL;

	g_return_val_if_fail(msg != NULL, FALSE);

	/* enlarge the size since fu_logitech_hidpp_transfer() returns the answer in the
	 * same message */
	fu_byte_array_set_size(msg, sizeof(FuLogitechHidppHidppMsg), 0);

	hidpp_msg = (FuLogitechHidppHidppMsg *)msg->data;
	hidpp_msg->hidpp_version = priv->hidpp_version;

	if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), hidpp_msg, error))
		return FALSE;

	/* validate and cleanup the function_id from Application ID */
	if ((hidpp_msg->function_id & 0x0f) != FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "expected application ID = %i, got %u",
			    FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID,
			    (guint)(hidpp_msg->function_id & 0x0f));
		return FALSE;
	}
	hidpp_msg->function_id &= 0xf0;

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_get_capabilities(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	g_autoptr(FuStructLogitechHidppRdfuGetCapabilities) msg =
	    fu_struct_logitech_hidpp_rdfu_get_capabilities_new();
	g_autoptr(GByteArray) response = NULL;

	priv->rdfu_capabilities = 0; /* set empty */

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00)
		return TRUE;

	fu_struct_logitech_hidpp_rdfu_get_capabilities_set_device_id(msg, priv->device_idx);
	fu_struct_logitech_hidpp_rdfu_get_capabilities_set_sub_id(msg, idx);

	g_debug("read capabilities");
	if (!fu_logitech_hidpp_device_transfer_msg(self, msg, error)) {
		g_prefix_error(error, "failed to get capabilities: ");
		return FALSE;
	}

	response = fu_struct_logitech_hidpp_rdfu_capabilities_parse(msg->data, msg->len, 0, error);
	if (response == NULL)
		return FALSE;
	priv->rdfu_capabilities =
	    fu_struct_logitech_hidpp_rdfu_capabilities_get_capabilities(response);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_start_dfu(FuLogitechHidppDevice *self,
					GByteArray *magic,
					GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	guint8 status;
	g_autoptr(FuStructLogitechHidppRdfuStartDfu) msg =
	    fu_struct_logitech_hidpp_rdfu_start_dfu_new();
	g_autoptr(GByteArray) response = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for startDfu");
		return FALSE;
	}

	fu_struct_logitech_hidpp_rdfu_start_dfu_set_device_id(msg, priv->device_idx);
	fu_struct_logitech_hidpp_rdfu_start_dfu_set_sub_id(msg, idx);
	fu_struct_logitech_hidpp_rdfu_start_dfu_set_fw_entity(msg, priv->cached_fw_entity);
	if (!fu_struct_logitech_hidpp_rdfu_start_dfu_set_magic(msg, magic->data, magic->len, error))
		return FALSE;

	g_debug("startDfu");
	if (!fu_logitech_hidpp_device_transfer_msg(self, msg, error)) {
		g_prefix_error(error, "startDfu failed: ");
		return FALSE;
	}

	response =
	    fu_struct_logitech_hidpp_rdfu_start_dfu_response_parse(msg->data, msg->len, 0, error);
	if (response == NULL)
		return FALSE;
	status = fu_struct_logitech_hidpp_rdfu_start_dfu_response_get_status_code(msg);
	if (status != FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DATA_TRANSFER_READY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR,
			    "unexpected status 0x%x = %s",
			    status,
			    fu_logitech_hidpp_rdfu_response_code_to_string(status));

		return FALSE;
	}

	priv->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER;

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_check_status(FuLogitechHidppDevice *self,
					   GByteArray *response,
					   GError **error);

static void
fu_logitech_hidpp_device_rdfu_set_state(FuLogitechHidppDevice *self, FuLogitechHidppRdfuState state)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	priv->rdfu_state = state;
	priv->rdfu_block = 0;
	priv->rdfu_pkt = 0;
}

static gboolean
fu_logitech_hidpp_device_rdfu_status_data_transfer_ready(FuLogitechHidppDevice *self,
							 GByteArray *response,
							 GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint16 block;
	g_autoptr(GByteArray) params = NULL;

	priv->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER;
	params = fu_struct_logitech_hidpp_rdfu_data_transfer_ready_parse(
	    response->data,
	    response->len,
	    FU_STRUCT_LOGITECH_HIDPP_RDFU_RESPONSE_OFFSET_STATUS_CODE,
	    error);
	if (params == NULL)
		return FALSE;

	block = fu_struct_logitech_hidpp_rdfu_data_transfer_ready_get_block_id(params);
	if (block != 0 && block <= priv->rdfu_block) {
		/* additional protection from misbehaviored devices */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_RESUME_DFU);
		return TRUE;
	}

	priv->rdfu_block = block;
	priv->rdfu_pkt = 0;
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_status_data_transfer_wait(FuLogitechHidppDevice *self,
							GByteArray *response,
							GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint retry = 0;
	g_autoptr(GByteArray) params = NULL;

	params = fu_struct_logitech_hidpp_rdfu_data_transfer_wait_parse(
	    response->data,
	    response->len,
	    FU_STRUCT_LOGITECH_HIDPP_RDFU_RESPONSE_OFFSET_STATUS_CODE,
	    error);
	if (params == NULL)
		return FALSE;

	/* set the delay to wait the next event */
	priv->rdfu_wait = fu_struct_logitech_hidpp_rdfu_data_transfer_wait_get_delay_ms(params);

	/* if we already in waiting loop just leave to avoid recursion */
	if (priv->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_WAIT)
		return TRUE;

	priv->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_WAIT;

	while (priv->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_WAIT) {
		g_autoptr(GByteArray) wait_response = NULL;
		g_autoptr(FuLogitechHidppHidppMsg) msg_in_wait = fu_logitech_hidpp_msg_new();
		g_autoptr(GError) error_local = NULL;

		/* wait for requested time or event */
		/* NB: waiting longer than `rdfu_wait` breaks the specification;
		 * unfortunately, some early firmware versions require this. */
		if (!fu_logitech_hidpp_receive(FU_UDEV_DEVICE(self),
					       msg_in_wait,
					       priv->rdfu_wait * 3, /* be tolerant */
					       &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
				g_debug("ignored error: %s", error_local->message);
				/* let's try to reset with getDfuStatus */
				fu_logitech_hidpp_device_rdfu_set_state(
				    self,
				    FU_LOGITECH_HIDPP_RDFU_STATE_RESUME_DFU);
				return TRUE;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		wait_response = fu_struct_logitech_hidpp_rdfu_response_parse(
		    (guint8 *)msg_in_wait,
		    fu_logitech_hidpp_msg_get_payload_length(msg_in_wait),
		    0,
		    error);
		if (wait_response == NULL)
			return FALSE;

		/* check the message and set the new delay if requested additional wait */
		if (!fu_logitech_hidpp_device_rdfu_check_status(self, wait_response, error))
			return FALSE;

		/* too lot attempts in a raw, let's fail everything */
		if (retry++ > FU_LOGITECH_HIDPP_DEVICE_RDFU_MAX_RETRIES) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "too lot of wait requests in a raw");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_status_pkt_ack(FuLogitechHidppDevice *self,
					     GByteArray *response,
					     GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint32 pkt;
	g_autoptr(GByteArray) params = NULL;

	priv->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER;
	params = fu_struct_logitech_hidpp_rdfu_dfu_transfer_pkt_ack_parse(
	    response->data,
	    response->len,
	    FU_STRUCT_LOGITECH_HIDPP_RDFU_RESPONSE_OFFSET_STATUS_CODE,
	    error);
	if (params == NULL)
		return FALSE;

	pkt = fu_struct_logitech_hidpp_rdfu_dfu_transfer_pkt_ack_get_pkt_number(params);
	/* expecting monotonic increase */
	if (pkt != priv->rdfu_pkt + 1) {
		/* additional protection from misbehaviored devices */
		if (pkt <= priv->rdfu_pkt) {
			fu_logitech_hidpp_device_rdfu_set_state(
			    self,
			    FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "expecting ack %u for block %u, got %u",
				    priv->rdfu_pkt + 1,
				    priv->rdfu_block,
				    pkt);
			return FALSE;
		}
		/* probably skipped the ACK, try to resume */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_RESUME_DFU);
		return TRUE;
	}

	priv->rdfu_pkt = pkt;

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_check_status(FuLogitechHidppDevice *self,
					   GByteArray *response,
					   GError **error)
{
	guint8 status = fu_struct_logitech_hidpp_rdfu_start_dfu_response_get_status_code(response);

	switch (status) {
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_NOT_STARTED:
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DATA_TRANSFER_READY:
		/* ready for transfer, next block requested */
		if (!fu_logitech_hidpp_device_rdfu_status_data_transfer_ready(self,
									      response,
									      error))
			return FALSE;
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DATA_TRANSFER_WAIT:
		/* device requested to wait */
		if (!fu_logitech_hidpp_device_rdfu_status_data_transfer_wait(self, response, error))
			return FALSE;
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_TRANSFER_PKT_ACK:
		/* ok to transfer next packet */
		if (!fu_logitech_hidpp_device_rdfu_status_pkt_ack(self, response, error))
			return FALSE;
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_TRANSFER_COMPLETE:
		/* success! Apply and reboot */
		fu_logitech_hidpp_device_rdfu_set_state(self, FU_LOGITECH_HIDPP_RDFU_STATE_APPLY);
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_INVALID_BLOCK:
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_STATE_ERROR:
		/* let's try to reset with getDfuStatus */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_RESUME_DFU);
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_APPLY_PENDING:
		/* device is already waiting to apply the unknown deferred update.
		 * Let's restart the update.*/
	default:
		/* reset state */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "unexpected status 0x%x (%s)",
			    status,
			    fu_logitech_hidpp_rdfu_response_code_to_string(status));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_get_dfu_status(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	g_autoptr(FuStructLogitechHidppRdfuGetDfuStatus) msg =
	    fu_struct_logitech_hidpp_rdfu_get_dfu_status_new();
	g_autoptr(GByteArray) response = NULL;
	g_autoptr(GError) error_local = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for getDfuStatus");
		return FALSE;
	}

	fu_struct_logitech_hidpp_rdfu_get_dfu_status_set_device_id(msg, priv->device_idx);
	fu_struct_logitech_hidpp_rdfu_get_dfu_status_set_sub_id(msg, idx);
	fu_struct_logitech_hidpp_rdfu_get_dfu_status_set_fw_entity(msg, priv->cached_fw_entity);

	g_debug("getDfuStatus for entity %u", priv->cached_fw_entity);
	if (!fu_logitech_hidpp_device_transfer_msg(self, msg, error)) {
		g_prefix_error(error, "getDfuStatus failed: ");
		return FALSE;
	}

	response = fu_struct_logitech_hidpp_rdfu_response_parse(msg->data, msg->len, 0, error);
	if (response == NULL)
		return FALSE;

	return fu_logitech_hidpp_device_rdfu_check_status(self, response, error);
}

static gboolean
fu_logitech_hidpp_device_rdfu_apply_dfu(FuLogitechHidppDevice *self,
					guint8 fw_entity,
					GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	guint8 flags = FU_LOGITECH_HIDPP_RDFU_APPLY_FLAG_FORCE_DFU_BIT;
	g_autoptr(FuStructLogitechHidppRdfuApplyDfu) msg =
	    fu_struct_logitech_hidpp_rdfu_apply_dfu_new();
	FuLogitechHidppHidppMsg *hidpp_msg = NULL;
	g_autoptr(GByteArray) response = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for startDfu");
		return FALSE;
	}

	if (priv->rdfu_state != FU_LOGITECH_HIDPP_RDFU_STATE_APPLY) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unable to apply the update");
		return FALSE;
	}

	g_debug("applyDfu for entity %u", fw_entity);

	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_device_id(msg, priv->device_idx);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_sub_id(msg, idx);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_fw_entity(msg, fw_entity);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_flags(msg, flags);

	/* reuse pre-rustgen send */
	hidpp_msg = (FuLogitechHidppHidppMsg *)msg->data;
	hidpp_msg->hidpp_version = priv->hidpp_version;
	/* don't expect the reply for forced applyDfu */
	if (!fu_logitech_hidpp_send(FU_UDEV_DEVICE(self),
				    hidpp_msg,
				    FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
				    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_transfer_pkt(FuLogitechHidppDevice *self,
					   const guint8 *data,
					   const gsize datasz,
					   GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	g_autoptr(FuStructLogitechHidppRdfuTransferDfuData) msg =
	    fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_new();
	g_autoptr(GByteArray) response = NULL;
	g_autoptr(GError) error_local = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for startDfu");
		return FALSE;
	}

	fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_set_device_id(msg, priv->device_idx);
	fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_set_sub_id(msg, idx);
	if (!fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_set_data(msg, data, datasz, error))
		return FALSE;

	g_debug("transferDfuData");
	if (!fu_logitech_hidpp_device_transfer_msg(self, msg, error)) {
		g_prefix_error(error, "transferDfuData failed: ");
		return FALSE;
	}

	response = fu_struct_logitech_hidpp_rdfu_response_parse(msg->data, msg->len, 0, error);
	if (response == NULL)
		return FALSE;

	return fu_logitech_hidpp_device_rdfu_check_status(self, response, error);
}

static gboolean
fu_logitech_hidpp_device_rdfu_transfer_data(FuLogitechHidppDevice *self,
					    GPtrArray *blocks,
					    GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	const GByteArray *block = NULL;
	g_autofree guint8 *data = g_malloc0(FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG);

	g_debug("send: block=%u, pkt=%04x", priv->rdfu_block, priv->rdfu_pkt);

	if (blocks->len < priv->rdfu_block) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "requested invalid block %u",
			    priv->rdfu_block);
		return FALSE;
	}
	block = (GByteArray *)blocks->pdata[priv->rdfu_block];

	if (!fu_memcpy_safe(data,
			    FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG,
			    0,
			    block->data,
			    block->len,
			    priv->rdfu_pkt * FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG,
			    FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG,
			    error)) {
		return FALSE;
	}

	return fu_logitech_hidpp_device_rdfu_transfer_pkt(self,
							  data,
							  FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG,
							  error);
}

static gboolean
fu_logitech_hidpp_device_feature_search(FuLogitechHidppDevice *self,
					guint16 feature,
					GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	FuLogitechHidppHidppMap *map;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();

	/* find the idx for the feature */
	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = priv->device_idx;
	msg->sub_id = 0x00;	      /* rootIndex */
	msg->function_id = 0x00 << 4; /* getFeature */
	msg->data[0] = feature >> 8;
	msg->data[1] = feature;
	msg->data[2] = 0x00;
	msg->hidpp_version = priv->hidpp_version;
	if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
		g_prefix_error(error,
			       "failed to get idx for feature %s [0x%04x]: ",
			       fu_logitech_hidpp_feature_to_string(feature),
			       feature);
		return FALSE;
	}

	/* zero index */
	if (msg->data[0] == 0x00) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "feature %s [0x%04x] not found",
			    fu_logitech_hidpp_feature_to_string(feature),
			    feature);
		return FALSE;
	}

	/* add to map */
	map = g_new0(FuLogitechHidppHidppMap, 1);
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
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);

	/* nearly... */
	fu_device_build_vendor_id_u16(device, "USB", 0x046D);

	/*
	 * All devices connected to a Bolt receiver share the same
	 * physical id, make them unique by using their pairing slot
	 * (device index) as a basis for their logical id.
	 */
	if (priv->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    priv->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER) {
		g_autoptr(GString) id_str = g_string_new(NULL);
		g_string_append_printf(id_str, "DEV_IDX=%d", priv->device_idx);
		fu_device_set_logical_id(device, id_str->str);
	}

	/* success */
	return TRUE;
}

static void
fu_logitech_hidpp_device_vid_pid_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	if (fu_device_get_pid(device) == 0x0)
		return;

	/* this is a non-standard extension, but essentially API */
	fu_device_add_instance_u16(device, "VID", fu_device_get_vid(device));
	fu_device_add_instance_u16(device, "PID", fu_device_get_pid(device));
	fu_device_build_instance_id(device, NULL, "UFY", "VID", "PID", NULL);
}

static gboolean
fu_logitech_hidpp_device_write_firmware_pkt(FuLogitechHidppDevice *self,
					    guint8 idx,
					    guint8 cmd,
					    const guint8 *data,
					    GError **error);

static gboolean
fu_logitech_hidpp_device_setup(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	const guint16 map_features[] = {FU_LOGITECH_HIDPP_FEATURE_GET_DEVICE_NAME_TYPE,
					FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO,
					FU_LOGITECH_HIDPP_FEATURE_BATTERY_LEVEL_STATUS,
					FU_LOGITECH_HIDPP_FEATURE_UNIFIED_BATTERY,
					FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL,
					FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_SIGNED,
					FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_BOLT,
					FU_LOGITECH_HIDPP_FEATURE_DFU,
					FU_LOGITECH_HIDPP_FEATURE_RDFU,
					FU_LOGITECH_HIDPP_FEATURE_ROOT};

	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE)) {
		priv->hidpp_version = FU_HIDPP_VERSION_BLE;
		priv->device_idx = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
		/*
		 * BLE devices might not be ready for ping right after
		 * they come up -> wait a bit before pinging.
		 */
		fu_device_sleep(device, 1000); /* ms */
	}
	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID))
		priv->device_idx = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;

	/* ping device to get HID++ version */
	if (!fu_logitech_hidpp_device_ping(self, error))
		return FALSE;

	/* did not get ID */
	if (priv->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no HID++ ID");
		return FALSE;
	}

	/* add known root for HID++2.0 */
	g_ptr_array_set_size(priv->feature_index, 0);
	if (priv->hidpp_version >= 2.f) {
		FuLogitechHidppHidppMap *map = g_new0(FuLogitechHidppHidppMap, 1);
		map->idx = 0x00;
		map->feature = FU_LOGITECH_HIDPP_FEATURE_ROOT;
		g_ptr_array_add(priv->feature_index, map);
	}

	/* map some *optional* HID++2.0 features we might use */
	for (guint i = 0; map_features[i] != FU_LOGITECH_HIDPP_FEATURE_ROOT; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_logitech_hidpp_device_feature_search(self, map_features[i], &error_local)) {
			g_debug("%s", error_local->message);
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				/* timed out, so not trying any more */
				break;
			}
		}
	}

	/* get the model ID, typically something like B3630000000000 */
	if (!fu_logitech_hidpp_device_fetch_model_id(self, error))
		return FALSE;

	/* try using HID++2.0 */
	idx = fu_logitech_hidpp_device_feature_get_idx(
	    self,
	    FU_LOGITECH_HIDPP_FEATURE_GET_DEVICE_NAME_TYPE);
	if (idx != 0x00) {
		const gchar *tmp;
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x02 << 4; /* getDeviceType */
		msg->hidpp_version = priv->hidpp_version;
		if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
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
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_remove_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifying");
	}
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_BOLT);
	if (idx == 0x00)
		idx = fu_logitech_hidpp_device_feature_get_idx(
		    self,
		    FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		/* check the feature is available */
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x00 << 4; /* getDfuStatus */
		msg->hidpp_version = priv->hidpp_version;
		if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
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
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);
	}
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
	if (idx != 0x00) {
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);
		if (fu_device_get_version(device) == NULL) {
			g_info("repairing device in bootloader mode");
			fu_device_set_version(FU_DEVICE(device), "MPK00.00_B0000");
		}
		/* we do not actually know which protocol when in recovery mode,
		 * so force the metadata to have the specific regex set up */
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifying");
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
	}

	/* get the firmware information */
	if (!fu_logitech_hidpp_device_fetch_firmware_info(self, error))
		return FALSE;

	/* get the battery level */
	if (!fu_logitech_hidpp_device_fetch_battery_level(self, error))
		return FALSE;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx != 0x00) {
		/* get RDFU capabilities */
		if (!fu_logitech_hidpp_device_rdfu_get_capabilities(self, error))
			return FALSE;
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.rdfu");
		fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LOGITECH_RDFU_FIRMWARE);
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	}
	/* poll for pings to track active state */
	fu_device_set_poll_interval(device, FU_HIDPP_DEVICE_POLLING_INTERVAL);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx != 0x00) {
		g_debug("RDFU supported, no need to switch to bootloader mode");
		return TRUE;
	}

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* these may require user action */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_BOLT);
	if (idx == 0x00)
		idx =
		    fu_logitech_hidpp_device_feature_get_idx(self,
							     FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		FuDevice *parent;
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		g_autoptr(GError) error_local = NULL;
		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_LONG;
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
		msg->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
			     FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
		if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, &error_local)) {
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
			fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
			fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
			fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
			if (!fu_device_emit_request(device, request, progress, error))
				return FALSE;
		}
		return TRUE;
	}

	/* this can reboot all by itself */
	idx =
	    fu_logitech_hidpp_device_feature_get_idx(self,
						     FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_LONG;
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
		msg->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SUB_ID;
		if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
			g_prefix_error(error, "failed to put device into DFU mode: ");
			return FALSE;
		}
		fu_device_sleep(device, 200); /* ms */
		return fu_logitech_hidpp_device_setup(FU_DEVICE(self), error);
	}

	/* we don't know how */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no method to detach");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_check_status(guint8 status_raw, GError **error)
{
	FuLogitechHidppStatus status = status_raw & 0x7f;
	const gchar *status_str = fu_logitech_hidpp_status_to_string(status);
	switch (status) {
	case FU_LOGITECH_HIDPP_STATUS_PACKET_SUCCESS:
	case FU_LOGITECH_HIDPP_STATUS_DFU_SUCCESS:
	case FU_LOGITECH_HIDPP_STATUS_DFU_SUCCESS_ENTITY_RESTART_REQUIRED:
	case FU_LOGITECH_HIDPP_STATUS_DFU_SUCCESS_SYSTEM_RESTART_REQUIRED:
		/* success */
		g_debug("ignoring: %s", status_str);
		return TRUE;
		break;
	case FU_LOGITECH_HIDPP_STATUS_COMMAND_IN_PROGRESS:
	case FU_LOGITECH_HIDPP_STATUS_WAIT_FOR_EVENT:
	case FU_LOGITECH_HIDPP_STATUS_BLOCKED_COMMAND:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, status_str);
		break;
	case FU_LOGITECH_HIDPP_STATUS_GENERIC_ERROR04:
	case FU_LOGITECH_HIDPP_STATUS_GENERIC_ERROR10:
	case FU_LOGITECH_HIDPP_STATUS_BAD_VOLTAGE:
	case FU_LOGITECH_HIDPP_STATUS_DFU_NOT_STARTED:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, status_str);
		break;
	case FU_LOGITECH_HIDPP_STATUS_UNKNOWN12:
	case FU_LOGITECH_HIDPP_STATUS_BAD_MAGIC_STRING:
	case FU_LOGITECH_HIDPP_STATUS_BAD_FIRMWARE:
	case FU_LOGITECH_HIDPP_STATUS_UNSUPPORTED_ENCRYPTION_MODE:
	case FU_LOGITECH_HIDPP_STATUS_UNSUPPORTED_COMMAND:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, status_str);
		break;
	case FU_LOGITECH_HIDPP_STATUS_INVALID:
	case FU_LOGITECH_HIDPP_STATUS_BAD_SEQUENCE_NUMBER:
	case FU_LOGITECH_HIDPP_STATUS_ADDRESS_OUT_OF_RANGE:
	case FU_LOGITECH_HIDPP_STATUS_UNALIGNED_ADDRESS:
	case FU_LOGITECH_HIDPP_STATUS_BAD_SIZE:
	case FU_LOGITECH_HIDPP_STATUS_MISSING_PROGRAM_DATA:
	case FU_LOGITECH_HIDPP_STATUS_MISSING_CHECK_DATA:
	case FU_LOGITECH_HIDPP_STATUS_FIRMWARE_CHECK_FAILURE:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, status_str);
		break;
	case FU_LOGITECH_HIDPP_STATUS_PROGRAM_FAILED_TO_WRITE:
	case FU_LOGITECH_HIDPP_STATUS_PROGRAM_FAILED_TO_VERIFY:
	case FU_LOGITECH_HIDPP_STATUS_ERASE_FAILURE:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, status_str);
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unhandled status value 0x%02x",
			    status);
		break;
	}
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_pkt(FuLogitechHidppDevice *self,
					    guint8 idx,
					    guint8 cmd,
					    const guint8 *data,
					    GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint32 packet_cnt;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;

	/* send firmware data */
	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_LONG;
	msg->device_id = priv->device_idx;
	msg->sub_id = idx;
	msg->function_id = cmd << 4; /* dfuStart or dfuCmdDataX */
	msg->hidpp_version = priv->hidpp_version;
	/* enable transfer workaround for devices paired to Bolt receiver */
	if (priv->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    priv->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER)
		msg->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_RETRY_STUCK;
	if (!fu_memcpy_safe(msg->data,
			    sizeof(msg->data),
			    0x0, /* dst */
			    data,
			    16,
			    0x0, /* src */
			    16,
			    error))
		return FALSE;
	if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, error)) {
		g_prefix_error(error, "failed to supply program data: ");
		return FALSE;
	}

	/* check error */
	if (!fu_memread_uint32_safe(msg->data,
				    sizeof(msg->data),
				    0x0,
				    &packet_cnt,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	g_debug("packet_cnt=0x%04x", packet_cnt);
	if (fu_logitech_hidpp_device_check_status(msg->data[4], &error_local))
		return TRUE;

	/* fatal error */
	if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BUSY)) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* wait for the HID++ notification */
	g_debug("ignoring: %s", error_local->message);
	for (guint retry = 0; retry < 10; retry++) {
		g_autoptr(FuLogitechHidppHidppMsg) msg2 = fu_logitech_hidpp_msg_new();
		msg2->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_FNCT_ID;
		if (!fu_logitech_hidpp_receive(FU_UDEV_DEVICE(self), msg2, 15000, error))
			return FALSE;
		if (fu_logitech_hidpp_msg_is_reply(msg, msg2)) {
			g_autoptr(GError) error2 = NULL;
			if (!fu_logitech_hidpp_device_check_status(msg2->data[4], &error2)) {
				g_debug("got %s, waiting a bit longer", error2->message);
				continue;
			}
			return TRUE;
		}
		g_debug("got wrong packet, continue to wait...");
	}

	/* nothing in the queue */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "failed to get event after timeout");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_dfu(FuDevice *device,
					    FuFirmware *firmware,
					    FuProgress *progress,
					    FwupdInstallFlags flags,
					    GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	gsize sz = 0;
	const guint8 *data;
	guint8 cmd = 0x04;
	guint8 idx;
	g_autoptr(GBytes) fw = NULL;

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no DFU feature available");
		return FALSE;
	}

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* flash hardware -- the first data byte is the fw entity */
	data = g_bytes_get_data(fw, &sz);
	if (priv->cached_fw_entity != data[0]) {
		g_debug("updating cached entity 0x%x with 0x%x", priv->cached_fw_entity, data[0]);
		priv->cached_fw_entity = data[0];
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (gsize i = 0; i < sz / 16; i++) {
		/* send packet and wait for reply */
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
fu_logitech_hidpp_device_write_firmware_rdfu(FuDevice *device,
					     FuFirmware *firmware,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	FuLogitechRdfuFirmware *entity_fw = NULL;
	guint8 idx;
	guint retry = 0;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) magic = NULL;
	g_autoptr(GPtrArray) blocks = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *entity_id = g_strdup_printf("%u", priv->cached_fw_entity);
	g_autofree gchar *model_id = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no RDFU feature available");
		return FALSE;
	}

	entity_fw =
	    (FuLogitechRdfuFirmware *)fu_firmware_get_image_by_id(firmware, entity_id, error);
	if (entity_fw == NULL)
		return FALSE;

	model_id = fu_logitech_rdfu_firmware_get_model_id(entity_fw, error);
	if (model_id == NULL)
		return FALSE;

	if (g_strcmp0(priv->model_id, model_id) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware for model %s, but the target is %s",
			    model_id,
			    priv->model_id);
		return FALSE;
	}

	magic = fu_logitech_rdfu_firmware_get_magic(entity_fw, error);
	if (magic == NULL)
		return FALSE;

	blocks = fu_logitech_rdfu_firmware_get_blocks(entity_fw, error);
	if (blocks == NULL)
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_id(progress, G_STRLOC);

	/* check if we in update mode already */
	if (!fu_logitech_hidpp_device_rdfu_get_dfu_status(self, &error_local)) {
		if (error_local->message != NULL)
			g_debug("forcing startDFU, reason %s", error_local->message);
		/* try to drop the inner state at device */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
	}
	/* device requested to start or restart for some reason */
	if (priv->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED) {
		if (!fu_logitech_hidpp_device_rdfu_start_dfu(self, magic, error))
			return FALSE;
	}

	while (priv->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER) {
		/* update progress-bar here to avoid jumps caused dfu-transfer-complete */
		fu_progress_set_percentage_full(progress, priv->rdfu_block, blocks->len);

		/* send packet and wait for reply */
		if (!fu_logitech_hidpp_device_rdfu_transfer_data(self, blocks, error))
			return FALSE;

		/* additional protection from misbehaviored devices */
		if (priv->rdfu_state != FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER) {
			if (!fu_logitech_hidpp_device_rdfu_get_dfu_status(self, error))
				return FALSE;

			/* too many soft restarts, let's fail everything */
			if (retry++ > FU_LOGITECH_HIDPP_DEVICE_RDFU_MAX_RETRIES) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "too lot recover attempts");
				return FALSE;
			}
		}
	}

	g_debug("RDFU supported, applying the update");
	if (!fu_logitech_hidpp_device_rdfu_apply_dfu(self, priv->cached_fw_entity, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	guint8 idx;
	g_autoptr(GBytes) fw = NULL;

	/* device should support either RDFU or DFU mode */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx != 0x00) {
		return fu_logitech_hidpp_device_write_firmware_rdfu(device,
								    firmware,
								    progress,
								    flags,
								    error);
	}

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
	if (idx != 0x00) {
		return fu_logitech_hidpp_device_write_firmware_dfu(device,
								   firmware,
								   progress,
								   flags,
								   error);
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "no DFU or RDFU feature available");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_reprobe_cb(FuDevice *device, gpointer user_data, GError **error)
{
	return fu_logitech_hidpp_device_setup(device, error);
}

gboolean
fu_logitech_hidpp_device_attach(FuLogitechHidppDevice *self,
				guint8 entity,
				FuProgress *progress,
				GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	FuDevice *device = FU_DEVICE(self);
	guint8 idx;
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		/* sanity check for DFU*/
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
			g_debug("already in runtime mode, skipping");
			return TRUE;
		}

		/* if we're in bootloader mode, we should be able to get this feature */
		idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
		if (idx == 0x00) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no DFU feature available");
			return FALSE;
		}

		/* reboot back into firmware mode */
		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_LONG;
		msg->device_id = priv->device_idx;
		msg->sub_id = idx;
		msg->function_id = 0x05 << 4; /* restart */
		msg->data[0] = entity;	      /* fwEntity */
		msg->hidpp_version = priv->hidpp_version;
		msg->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
			     FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SWID | /* inferred? */
			     FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
		if (!fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self), msg, &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("ignoring '%s' on reset", error_local->message);
			} else {
				g_propagate_prefixed_error(error,
							   g_steal_pointer(&error_local),
							   "failed to restart device: ");
				return FALSE;
			}
		}
	}

	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH)) {
		fu_device_set_poll_interval(device, 0);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
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
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);

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
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	if (g_strcmp0(key, "LogitechHidppModelId") == 0) {
		fu_logitech_hidpp_device_set_model_id(self, value);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_logitech_hidpp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_logitech_hidpp_device_finalize(GObject *object)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(object);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->feature_index);
	g_free(priv->model_id);
	G_OBJECT_CLASS(fu_logitech_hidpp_device_parent_class)->finalize(object);
}

static gboolean
fu_logitech_hidpp_device_cleanup(FuDevice *device,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	if (parent != NULL)
		fu_device_set_poll_interval(parent, FU_HIDPP_RECEIVER_RUNTIME_POLLING_INTERVAL);

	return TRUE;
}

static void
fu_logitech_hidpp_device_class_init(FuLogitechHidppDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logitech_hidpp_device_finalize;
	device_class->setup = fu_logitech_hidpp_device_setup;
	device_class->write_firmware = fu_logitech_hidpp_device_write_firmware;
	device_class->attach = fu_logitech_hidpp_device_attach_cached;
	device_class->detach = fu_logitech_hidpp_device_detach;
	device_class->poll = fu_logitech_hidpp_device_poll;
	device_class->to_string = fu_logitech_hidpp_device_to_string;
	device_class->probe = fu_logitech_hidpp_device_probe;
	device_class->set_quirk_kv = fu_logitech_hidpp_device_set_quirk_kv;
	device_class->cleanup = fu_logitech_hidpp_device_cleanup;
	device_class->set_progress = fu_logitech_hidpp_device_set_progress;
}

static void
fu_logitech_hidpp_device_init(FuLogitechHidppDevice *self)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	priv->device_idx = FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED;
	priv->feature_index = g_ptr_array_new_with_free_func(g_free);
	fu_logitech_hidpp_device_rdfu_set_state(self, FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_vid(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_VID);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_vendor(FU_DEVICE(self), "Logitech");
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID);
	fu_device_register_private_flag(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED);
	fu_device_register_private_flag(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_battery_threshold(FU_DEVICE(self), 20);
	g_signal_connect(FU_DEVICE(self),
			 "notify::vid",
			 G_CALLBACK(fu_logitech_hidpp_device_vid_pid_notify_cb),
			 NULL);
	g_signal_connect(FU_DEVICE(self),
			 "notify::pid",
			 G_CALLBACK(fu_logitech_hidpp_device_vid_pid_notify_cb),
			 NULL);
}

FuLogitechHidppDevice *
fu_logitech_hidpp_device_new(FuUdevDevice *parent)
{
	return g_object_new(FU_TYPE_HIDPP_DEVICE,
			    "proxy",
			    parent,
			    "device-file",
			    fu_udev_device_get_device_file(parent),
			    NULL);
}
