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
#include "fu-logitech-hidpp-runtime-bolt.h"
#include "fu-logitech-hidpp-struct.h"

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
	FuIOChannel *io_channel;
	gchar *model_id;
	GPtrArray *feature_index; /* of FuLogitechHidppHidppMap */
} FuLogitechHidppDevicePrivate;

typedef struct {
	guint8 idx;
	guint16 feature;
} FuLogitechHidppHidppMap;

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
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, &error_local)) {
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
fu_logitech_hidpp_device_close(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);

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
	if (!fu_logitech_hidpp_receive(priv->io_channel, msg, timeout, &error_local)) {
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

static gboolean
fu_logitech_hidpp_device_open(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_HIDPP_DEVICE(device);
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *devpath = fu_udev_device_get_device_file(FU_UDEV_DEVICE(device));

	if (devpath == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device path is not detected for '%s'",
			    fu_device_get_name(device));
		return FALSE;
	}

	/* open */
	priv->io_channel =
	    fu_io_channel_new_file(devpath,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (priv->io_channel == NULL)
		return FALSE;

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
				      (guint)FU_LOGITECH_HIDPP_DEVICE_VID,
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
	guint8 idx;
	guint8 entity_count;
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	gboolean radio_ok = FALSE;

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get the entity count */
	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
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

		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
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
fu_logitech_hidpp_device_fetch_model_id(FuLogitechHidppDevice *self, GError **error)
{
	FuLogitechHidppDevicePrivate *priv = GET_PRIVATE(self);
	guint8 idx;
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
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
		g_prefix_error(error, "failed to get the model ID: ");
		return FALSE;
	}

	/* ignore extendedModelID in data[13] */
	for (guint i = 7; i < 13; i++)
		g_string_append_printf(str, "%02X", msg->data[i]);
	fu_logitech_hidpp_device_set_model_id(self, str->str);

	/* add one more instance ID */
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", FU_LOGITECH_HIDPP_DEVICE_VID);
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
			if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
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
		if (fu_logitech_hidpp_transfer(priv->io_channel, msg, NULL)) {
			if (msg->data[0] != 0x7F)
				fu_device_set_battery_level(FU_DEVICE(self), msg->data[0]);
			else
				g_warning("unknown battery level: 0x%02x", msg->data[0]);
			return TRUE;
		}

		/* try HID++1.0 battery status instead */
		msg->function_id = FU_LOGITECH_HIDPP_REGISTER_BATTERY_STATUS << 4;
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

	/* check the kernel has CONFIG_HIDRAW */
	if (!g_file_test("/sys/class/hidraw", G_FILE_TEST_IS_DIR)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no kernel support for CONFIG_HIDRAW");
		return FALSE;
	}

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

	/* this is a non-standard extension */
	fu_device_add_instance_u16(FU_DEVICE(self), "VID", fu_device_get_vid(device));
	fu_device_add_instance_u16(FU_DEVICE(self), "PID", fu_device_get_pid(device));
	return fu_device_build_instance_id(FU_DEVICE(self), error, "UFY", "VID", "PID", NULL);
}

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
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
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
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
	if (idx != 0x00) {
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
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
		if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
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
fu_logitech_hidpp_device_check_status(guint8 status, GError **error)
{
	switch (status & 0x7f) {
	case 0x00:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "wait for event (command in progress)");
		break;
	case 0x04:
	case 0x10: /* unknown */
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "generic error");
		break;
	case 0x11:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "bad voltage (power too low?)");
		break;
	case 0x12:
	case 0x14: /* bad magic string */
	case 0x21: /* bad firmware */
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported firmware");
		break;
	case 0x13:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported encryption mode");
		break;
	case 0x15:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "erase failure");
		break;
	case 0x16:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "DFU not started");
		break;
	case 0x17:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "bad sequence number");
		break;
	case 0x18:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported command");
		break;
	case 0x19:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command in progress");
		break;
	case 0x1a:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "address out of range");
		break;
	case 0x1b:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unaligned address");
		break;
	case 0x1c:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bad size");
		break;
	case 0x1d:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "missing program data");
		break;
	case 0x1e:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "missing check data");
		break;
	case 0x1f:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "program failed to write");
		break;
	case 0x20:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "program failed to verify");
		break;
	case 0x22:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware check failure");
		break;
	case 0x23:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "blocked command (restart required)");
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
	if (!fu_logitech_hidpp_transfer(priv->io_channel, msg, error)) {
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
		if (!fu_logitech_hidpp_receive(priv->io_channel, msg2, 15000, error))
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
fu_logitech_hidpp_device_write_firmware(FuDevice *device,
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

	/* sanity check */
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
		     FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SWID | // inferred?
		     FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
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
		fu_device_sleep_full(FU_DEVICE(self), 1000, progress); /* ms */
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
	device_class->open = fu_logitech_hidpp_device_open;
	device_class->close = fu_logitech_hidpp_device_close;
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
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_vendor(FU_DEVICE(self), "Logitech");
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
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
}

FuLogitechHidppDevice *
fu_logitech_hidpp_device_new(FuUdevDevice *parent)
{
	FuLogitechHidppDevice *self = NULL;
	FuLogitechHidppDevicePrivate *priv;
	self = g_object_new(FU_TYPE_HIDPP_DEVICE,
			    "proxy",
			    parent,
			    "device-file",
			    fu_udev_device_get_device_file(parent),
			    NULL);
	priv = GET_PRIVATE(self);
	priv->io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(parent));
	return self;
}
