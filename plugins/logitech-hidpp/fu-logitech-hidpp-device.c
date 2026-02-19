/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-rdfu-struct.h"
#include "fu-logitech-hidpp-runtime-bolt.h"
#include "fu-logitech-hidpp-struct.h"
#include "fu-logitech-rdfu-entity.h"
#include "fu-logitech-rdfu-firmware.h"

struct _FuLogitechHidppDevice {
	FuDevice parent_instance;
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
	guint16 model_pid;
	GPtrArray *feature_index; /* of FuLogitechHidppHidppMap */
	FuLogitechHidppRdfuState rdfu_state;
	guint8 rdfu_capabilities;
	guint16 rdfu_block;
	guint32 rdfu_pkt;
	guint32 rdfu_wait;
};

typedef struct {
	guint8 idx;
	guint16 feature;
} FuLogitechHidppHidppMap;

#define FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG 16
/* max attempts to resume after non-critical errors */
#define FU_LOGITECH_HIDPP_DEVICE_RDFU_MAX_RETRIES 10

G_DEFINE_TYPE(FuLogitechHidppDevice, fu_logitech_hidpp_device, FU_TYPE_DEVICE)

void
fu_logitech_hidpp_device_set_device_idx(FuLogitechHidppDevice *self, guint8 device_idx)
{
	g_return_if_fail(FU_IS_LOGITECH_HIDPP_DEVICE(self));
	self->device_idx = device_idx;
}

guint16
fu_logitech_hidpp_device_get_hidpp_pid(FuLogitechHidppDevice *self)
{
	g_return_val_if_fail(FU_IS_LOGITECH_HIDPP_DEVICE(self), G_MAXUINT16);
	return self->hidpp_pid;
}

void
fu_logitech_hidpp_device_set_hidpp_pid(FuLogitechHidppDevice *self, guint16 hidpp_pid)
{
	g_return_if_fail(FU_IS_LOGITECH_HIDPP_DEVICE(self));
	self->hidpp_pid = hidpp_pid;
}

static void
fu_logitech_hidpp_device_set_model_id(FuLogitechHidppDevice *self, const gchar *model_id)
{
	g_return_if_fail(FU_IS_LOGITECH_HIDPP_DEVICE(self));
	if (g_strcmp0(self->model_id, model_id) == 0)
		return;
	g_free(self->model_id);
	self->model_id = g_strdup(model_id);
}

static const gchar *
fu_logitech_hidpp_device_get_icon(FuLogitechHidppDeviceKind kind)
{
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_KEYBOARD)
		return FU_DEVICE_ICON_INPUT_KEYBOARD;
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_REMOTE_CONTROL)
		return FU_DEVICE_ICON_PDA; /* ish */
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_NUMPAD)
		return FU_DEVICE_ICON_INPUT_DIALPAD;
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_MOUSE)
		return FU_DEVICE_ICON_INPUT_MOUSE;
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_TOUCHPAD)
		return FU_DEVICE_ICON_INPUT_TOUCHPAD;
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_TRACKBALL)
		return FU_DEVICE_ICON_INPUT_MOUSE; /* ish */
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_PRESENTER)
		return FU_DEVICE_ICON_PDA; /* ish */
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_RECEIVER)
		return FU_DEVICE_ICON_USB_RECEIVER;
	return NULL;
}

static const gchar *
fu_logitech_hidpp_device_get_summary(FuLogitechHidppDeviceKind kind)
{
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_KEYBOARD)
		return "Unifying Keyboard";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_REMOTE_CONTROL)
		return "Unifying Remote Control";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_NUMPAD)
		return "Unifying Number Pad";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_MOUSE)
		return "Unifying Mouse";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_TOUCHPAD)
		return "Unifying Touchpad";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_TRACKBALL)
		return "Unifying Trackball";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_PRESENTER)
		return "Unifying Presenter";
	if (kind == FU_LOGITECH_HIDPP_DEVICE_KIND_RECEIVER)
		return "Unifying Receiver";
	return NULL;
}

static gboolean
fu_logitech_hidpp_device_ping(FuLogitechHidppDevice *self, GError **error)
{
	FuDevice *proxy;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	const guint8 buf[] = {
	    0x00,
	    0x00,
	    0xAA, /* user-selected value */
	};

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, 0x00);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x01 << 4); /* ping */
	if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* handle failure */
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    &error_local);
	if (st_rsp == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			self->hidpp_version = FU_LOGITECH_HIDPP_VERSION_1;
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

	/* if the device index is unset, grab it from the reply */
	if (self->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    fu_struct_logitech_hidpp_msg_get_device_id(st_req) !=
		FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED) {
		self->device_idx = fu_struct_logitech_hidpp_msg_get_device_id(st_rsp);
		g_debug("device index is %02x", self->device_idx);
	}

	/* format version in BCD format */
	if (self->hidpp_version != FU_LOGITECH_HIDPP_VERSION_BLE) {
		const guint8 *data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
		self->hidpp_version = data[0];
		/* minor version is in st_req->data[1] */;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_poll(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	FuDevice *proxy;
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* not predictable for time */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* open */
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	/* flush pending data */
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	st_rsp = fu_logitech_hidpp_receive(FU_UDEV_DEVICE(proxy), timeout, &error_local);
	if (st_rsp == NULL) {
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
	if (self->feature_index->len == 0) {
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
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "HidppVersion", self->hidpp_version);
	fwupd_codec_string_append_int(str, idt, "HidppPid", self->hidpp_pid);
	fwupd_codec_string_append_hex(str, idt, "DeviceIdx", self->device_idx);
	fwupd_codec_string_append_hex(str, idt, "ModelPid", self->model_pid);
	fwupd_codec_string_append(str, idt, "ModelId", self->model_id);
	for (guint i = 0; i < self->feature_index->len; i++) {
		FuLogitechHidppHidppMap *map = g_ptr_array_index(self->feature_index, i);
		fu_logitech_hidpp_device_map_to_string(map, idt, str);
	}
}

static guint8
fu_logitech_hidpp_device_feature_get_idx(FuLogitechHidppDevice *self, guint16 feature)
{
	for (guint i = 0; i < self->feature_index->len; i++) {
		FuLogitechHidppHidppMap *map = g_ptr_array_index(self->feature_index, i);
		if (map->feature == feature)
			return map->idx;
	}
	return 0x00;
}

static gboolean
fu_logitech_hidpp_device_fetch_firmware_info(FuLogitechHidppDevice *self, GError **error)
{
	FuDevice *proxy;
	guint8 idx;
	guint8 entity_count;
	g_autoptr(FuStructLogitechHidppMsg) st_count = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_count_rsp = NULL;
	gboolean app_ok = FALSE;
	const guint8 *data;

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* get the entity count */
	fu_struct_logitech_hidpp_msg_set_report_id(st_count, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_count, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_count, idx);
	fu_struct_logitech_hidpp_msg_set_function_id(st_count, 0x00 << 4); /* getCount */

	st_count_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
						  st_count,
						  self->hidpp_version,
						  FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
						  error);
	if (st_count_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get firmware count: ");
		return FALSE;
	}
	data = fu_struct_logitech_hidpp_msg_get_data(st_count_rsp, NULL);
	entity_count = data[0];
	g_debug("firmware entity count is %u", entity_count);

	/* get firmware, bootloader, hardware versions */
	for (guint8 i = 0; i < entity_count; i++) {
		guint16 build;
		g_autofree gchar *version = NULL;
		g_autofree gchar *name = NULL;
		g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
		g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
		const guint8 buf[] = {i};

		fu_struct_logitech_hidpp_msg_set_report_id(st_req,
							   FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
		fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
		fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
		fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x01 << 4); /* getInfo */
		if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
			return FALSE;
		st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
						    st_req,
						    self->hidpp_version,
						    FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_FNCT_ID,
						    error);
		if (st_rsp == NULL) {
			g_prefix_error_literal(error, "failed to get firmware info: ");
			return FALSE;
		}

		/* use the single available slot, otherwise -- the slot which is not active */
		data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
		if (data[0] == 0) {
			if (!app_ok || FU_BIT_IS_CLEAR(data[8], 0))
				self->cached_fw_entity = i;
			app_ok = TRUE;
		}
		if (data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x00 && data[4] == 0x00 &&
		    data[5] == 0x00 && data[6] == 0x00 && data[7] == 0x00) {
			g_debug("no version set for entity %u", i);
			continue;
		}
		name = g_strdup_printf("%c%c%c", data[1], data[2], data[3]);
		build = ((guint16)data[6]) << 8 | data[7];
		version = fu_logitech_hidpp_format_version(name, data[4], data[5], build);
		g_debug("firmware entity 0x%02x version is %s", i, version);
		if (data[0] == 0) {
			/* set version from the active entity */
			if (FU_BIT_IS_SET(data[8], 0))
				fu_device_set_version(FU_DEVICE(self), version);
		} else if (data[0] == 1) {
			fu_device_set_version_bootloader(FU_DEVICE(self), version);
		} else if (data[0] == 2) {
			fu_device_set_metadata(FU_DEVICE(self), "version-hw", version);
		}
	}

	/* not an error, the device just doesn't support this */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_fetch_model_id(FuLogitechHidppDevice *self, GError **error)
{
	FuDevice *proxy;
	const guint8 *data;
	guint8 idx;
	guint64 pid_tmp = 0;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* get the (optional) feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x00 << 4); /* getDeviceInfo */
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get the model ID: ");
		return FALSE;
	}

	/* ignore extendedModelID in data[13] */
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
	for (guint i = 7; i < 13; i++)
		g_string_append_printf(str, "%02X", data[i]);
	fu_logitech_hidpp_device_set_model_id(self, str->str);

	/* truncate to 4 chars and convert to a PID */
	g_string_set_size(str, 4);
	if (!fu_strtoull(str->str, &pid_tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_16, error)) {
		g_prefix_error_literal(error, "failed to parse the model ID: ");
		return FALSE;
	}
	self->model_pid = pid_tmp;

	/* add one more instance ID */
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", fu_device_get_vid(FU_DEVICE(self)));
	fu_device_add_instance_str(FU_DEVICE(self), "MOD", self->model_id);
	return fu_device_build_instance_id(FU_DEVICE(self), error, "HIDRAW", "VEN", "MOD", NULL);
}

static gboolean
fu_logitech_hidpp_device_ensure_capabilities(FuLogitechHidppDevice *self,
					     guint8 idx,
					     guint8 *capabilities,
					     GError **error)
{
	FuDevice *proxy;
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x00 << 4);

	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get battery info: ");
		return FALSE;
	}
	buf = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &bufsz);
	return fu_memread_uint8_safe(buf, bufsz, 0x1, capabilities, error);
}

static gboolean
fu_logitech_hidpp_device_ensure_battery_level_unified(FuLogitechHidppDevice *self,
						      guint8 idx,
						      GError **error)
{
	FuDevice *proxy;
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 capabilities = 0;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

	/* GetCapabilities */
	if (!fu_logitech_hidpp_device_ensure_capabilities(self, idx, &capabilities, error))
		return FALSE;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* GetStatus */
	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x01 << 4); /* get_status */

	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get battery info: ");
		return FALSE;
	}
	buf = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &bufsz);
	if (capabilities & 0x02) { /* state of charge */
		guint8 battery_pc = 0;
		if (!fu_memread_uint8_safe(buf, bufsz, 0x0, &battery_pc, error))
			return FALSE;
		fu_device_set_battery_level(FU_DEVICE(self), battery_pc);
	} else {
		guint8 battery_level = 0;
		if (!fu_memread_uint8_safe(buf, bufsz, 0x1, &battery_level, error))
			return FALSE;
		switch (battery_level) {
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
			g_warning("unknown battery level: 0x%02x", battery_level);
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_ensure_battery_level_status(FuLogitechHidppDevice *self,
						     guint8 idx,
						     GError **error)
{
	FuDevice *proxy;
	const guint8 *data;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x00 << 4); /* GetBatteryLevelStatus */

	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get battery info: ");
		return FALSE;
	}
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
	if (data[0] != 0x00)
		fu_device_set_battery_level(FU_DEVICE(self), data[0]);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_ensure_battery_level_mileage(FuLogitechHidppDevice *self, GError **error)
{
	FuDevice *proxy;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, FU_LOGITECH_HIDPP_SUBID_GET_REGISTER);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req,
						     FU_LOGITECH_HIDPP_REGISTER_BATTERY_MILEAGE
							 << 4);

	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    NULL);
	if (st_rsp != NULL) {
		const guint8 *data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
		if (data[0] != 0x7F)
			fu_device_set_battery_level(FU_DEVICE(self), data[0]);
		else
			g_warning("unknown battery level: 0x%02x", data[0]);
		return TRUE;
	}

	/* try HID++1.0 battery status instead */
	fu_struct_logitech_hidpp_msg_set_function_id(st_req,
						     FU_LOGITECH_HIDPP_REGISTER_BATTERY_STATUS
							 << 4);
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    NULL);
	if (st_rsp != NULL) {
		const guint8 *data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
		switch (data[0]) {
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
			g_warning("unknown battery percentage: 0x%02x", data[0]);
			break;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_ensure_battery_level(FuLogitechHidppDevice *self, GError **error)
{
	guint8 idx;

	/* this does not make sense */
	if (self->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED ||
	    self->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER)
		return TRUE;

	/* HID++1.0 */
	if (self->hidpp_version == 1)
		return fu_logitech_hidpp_device_ensure_battery_level_mileage(self, error);

	/* try the HID++2.0 Unified Battery feature first */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_UNIFIED_BATTERY);
	if (idx != 0x00)
		return fu_logitech_hidpp_device_ensure_battery_level_unified(self, idx, error);

	/* fall back to the legacy HID++2.0 Battery Level feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(
	    self,
	    FU_LOGITECH_HIDPP_FEATURE_BATTERY_LEVEL_STATUS);
	if (idx != 0x00)
		return fu_logitech_hidpp_device_ensure_battery_level_status(self, idx, error);

	/* not an error, the device just doesn't support any of the methods */
	return TRUE;
}

/* wrapper function to reuse the pre-rustgen communication */
static GByteArray *
fu_logitech_hidpp_device_transfer_raw(FuLogitechHidppDevice *self, GByteArray *buf, GError **error)
{
	FuDevice *proxy;
	g_autoptr(FuStructLogitechHidppMsg) st_req = NULL;
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

	g_return_val_if_fail(buf != NULL, NULL);

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return NULL;

	/* enlarge the size to make a valid FuStructLogitechHidppMsg */
	fu_byte_array_set_size(buf, FU_STRUCT_LOGITECH_HIDPP_MSG_SIZE, 0);
	st_req = fu_struct_logitech_hidpp_msg_parse(buf->data, buf->len, 0x0, error);
	if (st_req == NULL)
		return NULL;
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_FNCT_ID,
					    error);
	if (st_rsp == NULL)
		return NULL;
	return g_byte_array_ref(st_rsp->buf);
}

static gboolean
fu_logitech_hidpp_device_rdfu_get_capabilities(FuLogitechHidppDevice *self, GError **error)
{
	guint8 idx;
	g_autoptr(FuStructLogitechHidppRdfuGetCapabilities) st_req =
	    fu_struct_logitech_hidpp_rdfu_get_capabilities_new();
	g_autoptr(FuStructLogitechHidppRdfuCapabilities) st_rsp = NULL;
	g_autoptr(GByteArray) buf_rsp = NULL;

	self->rdfu_capabilities = 0; /* set empty */

	/* get the feature index */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00)
		return TRUE;

	fu_struct_logitech_hidpp_rdfu_get_capabilities_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_rdfu_get_capabilities_set_sub_id(st_req, idx);

	g_debug("read capabilities");
	buf_rsp = fu_logitech_hidpp_device_transfer_raw(self, st_req->buf, error);
	if (buf_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get capabilities: ");
		return FALSE;
	}

	st_rsp =
	    fu_struct_logitech_hidpp_rdfu_capabilities_parse(buf_rsp->data, buf_rsp->len, 0, error);
	if (st_rsp == NULL)
		return FALSE;
	self->rdfu_capabilities =
	    fu_struct_logitech_hidpp_rdfu_capabilities_get_capabilities(st_rsp);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_start_dfu(FuLogitechHidppDevice *self,
					GByteArray *magic,
					GError **error)
{
	guint8 idx;
	guint8 status;
	g_autoptr(FuStructLogitechHidppRdfuStartDfu) st_req =
	    fu_struct_logitech_hidpp_rdfu_start_dfu_new();
	g_autoptr(FuStructLogitechHidppRdfuStartDfuResponse) st_rsp = NULL;
	g_autoptr(GByteArray) buf_rsp = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for startDfu");
		return FALSE;
	}

	fu_struct_logitech_hidpp_rdfu_start_dfu_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_rdfu_start_dfu_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_rdfu_start_dfu_set_fw_entity(st_req, self->cached_fw_entity);
	if (!fu_struct_logitech_hidpp_rdfu_start_dfu_set_magic(st_req,
							       magic->data,
							       magic->len,
							       error))
		return FALSE;

	g_debug("startDfu");
	buf_rsp = fu_logitech_hidpp_device_transfer_raw(self, st_req->buf, error);
	if (buf_rsp == NULL) {
		g_prefix_error_literal(error, "startDfu failed: ");
		return FALSE;
	}

	st_rsp = fu_struct_logitech_hidpp_rdfu_start_dfu_response_parse(buf_rsp->data,
									buf_rsp->len,
									0,
									error);
	if (st_rsp == NULL)
		return FALSE;
	if ((fu_struct_logitech_hidpp_rdfu_start_dfu_response_get_function_id(st_rsp) &
	     FU_LOGITECH_HIDPP_RDFU_FUNC_START_DFU) == 0) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "unexpected function_id 0x%x",
		    fu_struct_logitech_hidpp_rdfu_start_dfu_response_get_function_id(st_rsp));
		return FALSE;
	}
	status = fu_struct_logitech_hidpp_rdfu_start_dfu_response_get_status_code(st_rsp);
	if (status != FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DATA_TRANSFER_READY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR,
			    "unexpected status 0x%x = %s",
			    status,
			    fu_logitech_hidpp_rdfu_response_code_to_string(status));
		return FALSE;
	}

	self->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER;

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_check_status(FuLogitechHidppDevice *self,
					   FuStructLogitechHidppRdfuResponse *st_rsp,
					   GError **error);

static void
fu_logitech_hidpp_device_rdfu_set_state(FuLogitechHidppDevice *self, FuLogitechHidppRdfuState state)
{
	self->rdfu_state = state;
	self->rdfu_block = 0;
	self->rdfu_pkt = 0;
}

static gboolean
fu_logitech_hidpp_device_rdfu_status_data_transfer_ready(FuLogitechHidppDevice *self,
							 FuStructLogitechHidppRdfuResponse *st_rsp,
							 GError **error)
{
	guint16 block;
	g_autoptr(FuStructLogitechHidppRdfuDataTransferReady) st_params = NULL;

	self->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER;
	st_params = fu_struct_logitech_hidpp_rdfu_data_transfer_ready_parse(
	    st_rsp->buf->data,
	    st_rsp->buf->len,
	    FU_STRUCT_LOGITECH_HIDPP_RDFU_RESPONSE_OFFSET_STATUS_CODE,
	    error);
	if (st_params == NULL)
		return FALSE;

	block = fu_struct_logitech_hidpp_rdfu_data_transfer_ready_get_block_id(st_params);
	if (block != 0 && block <= self->rdfu_block) {
		/* additional protection from misbehaviored devices */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_RESUME_DFU);
		return TRUE;
	}

	self->rdfu_block = block;
	self->rdfu_pkt = 0;
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_status_data_transfer_wait(FuLogitechHidppDevice *self,
							FuStructLogitechHidppRdfuResponse *st_rsp,
							GError **error)
{
	FuDevice *proxy;
	guint retry = 0;
	g_autoptr(FuStructLogitechHidppRdfuDataTransferWait) st_params = NULL;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	st_params = fu_struct_logitech_hidpp_rdfu_data_transfer_wait_parse(
	    st_rsp->buf->data,
	    st_rsp->buf->len,
	    FU_STRUCT_LOGITECH_HIDPP_RDFU_RESPONSE_OFFSET_STATUS_CODE,
	    error);
	if (st_params == NULL)
		return FALSE;

	/* set the delay to wait the next event */
	self->rdfu_wait = fu_struct_logitech_hidpp_rdfu_data_transfer_wait_get_delay_ms(st_params);

	/* if we already in waiting loop just leave to avoid recursion */
	if (self->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_WAIT)
		return TRUE;

	self->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_WAIT;

	while (self->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_WAIT) {
		g_autoptr(FuStructLogitechHidppRdfuResponse) st_wait_res = NULL;
		g_autoptr(FuStructLogitechHidppMsg) st_in_wait = NULL;
		g_autoptr(GError) error_local = NULL;

		/* wait for requested time or event */
		/* NB: waiting longer than `rdfu_wait` breaks the specification;
		 * unfortunately, some early firmware versions require this. */
		st_in_wait = fu_logitech_hidpp_receive(FU_UDEV_DEVICE(proxy),
						       self->rdfu_wait * 3, /* be tolerant */
						       &error_local);
		if (st_in_wait == NULL) {
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

		st_wait_res = fu_struct_logitech_hidpp_rdfu_response_parse(st_in_wait->buf->data,
									   st_in_wait->buf->len,
									   0,
									   error);
		if (st_wait_res == NULL)
			return FALSE;

		/* check the message and set the new delay if requested additional wait */
		if (!fu_logitech_hidpp_device_rdfu_check_status(self, st_wait_res, error))
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
					     FuStructLogitechHidppRdfuResponse *st_rsp,
					     GError **error)
{
	guint32 pkt;
	g_autoptr(FuStructLogitechHidppRdfuDfuTransferPktAck) st_params = NULL;

	self->rdfu_state = FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER;
	st_params = fu_struct_logitech_hidpp_rdfu_dfu_transfer_pkt_ack_parse(
	    st_rsp->buf->data,
	    st_rsp->buf->len,
	    FU_STRUCT_LOGITECH_HIDPP_RDFU_RESPONSE_OFFSET_STATUS_CODE,
	    error);
	if (st_params == NULL)
		return FALSE;

	pkt = fu_struct_logitech_hidpp_rdfu_dfu_transfer_pkt_ack_get_pkt_number(st_params);
	/* expecting monotonic increase */
	if (pkt != self->rdfu_pkt + 1) {
		/* additional protection from misbehaviored devices */
		if (pkt <= self->rdfu_pkt) {
			fu_logitech_hidpp_device_rdfu_set_state(
			    self,
			    FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "expecting ack %u for block %u, got %u",
				    self->rdfu_pkt + 1,
				    self->rdfu_block,
				    pkt);
			return FALSE;
		}
		/* probably skipped the ACK, try to resume */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_RESUME_DFU);
		return TRUE;
	}

	self->rdfu_pkt = pkt;

	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_rdfu_check_status(FuLogitechHidppDevice *self,
					   FuStructLogitechHidppRdfuResponse *st_rsp,
					   GError **error)
{
	guint8 status = fu_struct_logitech_hidpp_rdfu_response_get_status_code(st_rsp);

	switch (status) {
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_NOT_STARTED:
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DATA_TRANSFER_READY:
		/* ready for transfer, next block requested */
		if (!fu_logitech_hidpp_device_rdfu_status_data_transfer_ready(self, st_rsp, error))
			return FALSE;
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DATA_TRANSFER_WAIT:
		/* device requested to wait */
		if (!fu_logitech_hidpp_device_rdfu_status_data_transfer_wait(self, st_rsp, error))
			return FALSE;
		break;
	case FU_LOGITECH_HIDPP_RDFU_RESPONSE_CODE_DFU_TRANSFER_PKT_ACK:
		/* ok to transfer next packet */
		if (!fu_logitech_hidpp_device_rdfu_status_pkt_ack(self, st_rsp, error))
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
	guint8 idx;
	g_autoptr(FuStructLogitechHidppRdfuGetDfuStatus) st_req =
	    fu_struct_logitech_hidpp_rdfu_get_dfu_status_new();
	g_autoptr(FuStructLogitechHidppRdfuResponse) st_rsp = NULL;
	g_autoptr(GByteArray) buf_rsp = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for getDfuStatus");
		return FALSE;
	}

	fu_struct_logitech_hidpp_rdfu_get_dfu_status_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_rdfu_get_dfu_status_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_rdfu_get_dfu_status_set_fw_entity(st_req, self->cached_fw_entity);

	g_debug("getDfuStatus for entity %u", self->cached_fw_entity);
	buf_rsp = fu_logitech_hidpp_device_transfer_raw(self, st_req->buf, error);
	if (buf_rsp == NULL) {
		g_prefix_error_literal(error, "getDfuStatus failed: ");
		return FALSE;
	}

	st_rsp =
	    fu_struct_logitech_hidpp_rdfu_response_parse(buf_rsp->data, buf_rsp->len, 0, error);
	if (st_rsp == NULL)
		return FALSE;

	return fu_logitech_hidpp_device_rdfu_check_status(self, st_rsp, error);
}

static gboolean
fu_logitech_hidpp_device_rdfu_apply_dfu(FuLogitechHidppDevice *self,
					guint8 fw_entity,
					GError **error)
{
	FuDevice *proxy;
	guint8 idx;
	g_autoptr(FuStructLogitechHidppMsg) st_msg = NULL;
	g_autoptr(FuStructLogitechHidppRdfuApplyDfu) st_req =
	    fu_struct_logitech_hidpp_rdfu_apply_dfu_new();

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for startDfu");
		return FALSE;
	}

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	if (self->rdfu_state != FU_LOGITECH_HIDPP_RDFU_STATE_APPLY) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unable to apply the update");
		return FALSE;
	}

	g_debug("applyDfu for entity %u", fw_entity);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_fw_entity(st_req, fw_entity);
	fu_struct_logitech_hidpp_rdfu_apply_dfu_set_flags(
	    st_req,
	    FU_LOGITECH_HIDPP_RDFU_APPLY_FLAG_FORCE_DFU_BIT);

	/* enlarge the size to make a valid FuStructLogitechHidppMsg */
	fu_byte_array_set_size(st_req->buf, FU_STRUCT_LOGITECH_HIDPP_MSG_SIZE, 0);
	st_msg =
	    fu_struct_logitech_hidpp_msg_parse(st_req->buf->data, st_req->buf->len, 0x0, error);
	if (st_msg == NULL)
		return FALSE;

	/* don't expect the reply for forced applyDfu */
	return fu_logitech_hidpp_send(FU_UDEV_DEVICE(proxy),
				      st_msg,
				      self->hidpp_version,
				      FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
				      FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
				      error);
}

static gboolean
fu_logitech_hidpp_device_rdfu_transfer_pkt(FuLogitechHidppDevice *self,
					   const guint8 *data,
					   const gsize datasz,
					   GError **error)
{
	guint8 idx;
	g_autoptr(FuStructLogitechHidppRdfuTransferDfuData) st_req =
	    fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_new();
	g_autoptr(FuStructLogitechHidppRdfuResponse) st_rsp = NULL;
	g_autoptr(GByteArray) buf_rsp = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "RDFU feature is required for startDfu");
		return FALSE;
	}

	fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_set_sub_id(st_req, idx);
	if (!fu_struct_logitech_hidpp_rdfu_transfer_dfu_data_set_data(st_req, data, datasz, error))
		return FALSE;

	g_debug("transferDfuData");
	buf_rsp = fu_logitech_hidpp_device_transfer_raw(self, st_req->buf, error);
	if (buf_rsp == NULL) {
		g_prefix_error_literal(error, "transferDfuData failed: ");
		return FALSE;
	}
	st_rsp =
	    fu_struct_logitech_hidpp_rdfu_response_parse(buf_rsp->data, buf_rsp->len, 0, error);
	if (st_rsp == NULL)
		return FALSE;

	return fu_logitech_hidpp_device_rdfu_check_status(self, st_rsp, error);
}

static gboolean
fu_logitech_hidpp_device_rdfu_transfer_data(FuLogitechHidppDevice *self,
					    GPtrArray *blocks,
					    GError **error)
{
	GByteArray *block;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_debug("send: block=%u, pkt=%04x", self->rdfu_block, self->rdfu_pkt);

	if (self->rdfu_block >= blocks->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "requested invalid block %u",
			    self->rdfu_block);
		return FALSE;
	}
	block = g_ptr_array_index(blocks, self->rdfu_block);
	if (!fu_byte_array_append_safe(buf,
				       block->data,
				       block->len,
				       self->rdfu_pkt * FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG,
				       FU_LOGITECH_HIDPP_DEVICE_DATA_PKT_LONG,
				       error)) {
		g_prefix_error(error, "failed to append block %u: ", self->rdfu_block);
		return FALSE;
	}
	return fu_logitech_hidpp_device_rdfu_transfer_pkt(self, buf->data, buf->len, error);
}

static gboolean
fu_logitech_hidpp_device_feature_search(FuLogitechHidppDevice *self,
					guint16 feature,
					GError **error)
{
	FuLogitechHidppHidppMap *map;
	FuDevice *proxy;
	const guint8 *data;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

	/* find the idx for the feature */
	const guint8 buf[] = {
	    feature >> 8,
	    feature,
	    0x00,
	};

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, 0x00);		 /* rootIndex */
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x00 << 4); /* getFeature */
	if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error(error,
			       "failed to get idx for feature %s [0x%04x]: ",
			       fu_logitech_hidpp_feature_to_string(feature),
			       feature);
		return FALSE;
	}

	/* zero index */
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
	if (data[0] == 0x00) {
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
	map->idx = data[0];
	map->feature = feature;
	g_ptr_array_add(self->feature_index, map);
	g_debug("added feature %s [0x%04x] as idx %02x",
		fu_logitech_hidpp_feature_to_string(feature),
		feature,
		map->idx);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_probe(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);

	/* nearly... */
	fu_device_build_vendor_id_u16(device, "USB", 0x046D);

	/*
	 * All devices connected to a Bolt receiver share the same
	 * physical id, make them unique by using their pairing slot
	 * (device index) as a basis for their logical id.
	 */
	if (self->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    self->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER) {
		g_autoptr(GString) id_str = g_string_new(NULL);
		g_string_append_printf(id_str, "DEV_IDX=%d", self->device_idx);
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

static void
fu_logitech_hidpp_device_proxy_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	FuDevice *proxy;

	if (fu_device_get_proxy_gtype(device) == G_TYPE_INVALID)
		return;
	proxy = fu_device_get_proxy(device, NULL);
	if (proxy == NULL)
		return;

	/* open the proxy with a writable file descriptor */
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(proxy), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static gboolean
fu_logitech_hidpp_device_setup(FuDevice *device, GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	FuDevice *proxy;
	guint8 idx;
	const guint16 map_features[] = {
	    FU_LOGITECH_HIDPP_FEATURE_GET_DEVICE_NAME_TYPE,
	    FU_LOGITECH_HIDPP_FEATURE_I_FIRMWARE_INFO,
	    FU_LOGITECH_HIDPP_FEATURE_BATTERY_LEVEL_STATUS,
	    FU_LOGITECH_HIDPP_FEATURE_UNIFIED_BATTERY,
	    FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL,
	    FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_SIGNED,
	    FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_BOLT,
	    FU_LOGITECH_HIDPP_FEATURE_DFU,
	    FU_LOGITECH_HIDPP_FEATURE_RDFU,
	    FU_LOGITECH_HIDPP_FEATURE_ROOT,
	};

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE)) {
		self->hidpp_version = FU_LOGITECH_HIDPP_VERSION_BLE;
		self->device_idx = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
		/*
		 * BLE devices might not be ready for ping right after
		 * they come up -> wait a bit before pinging.
		 */
		fu_device_sleep(device, 1000); /* ms */
	}
	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID))
		self->device_idx = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;

	/* the bolt receiver seems to report junk if we query it too quickly -- we're probably
	 * racing with wither the kernel or Solaar */
	if (self->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER)
		fu_device_sleep(device, 50); /* ms */

	/* ping device to get HID++ version */
	if (!fu_logitech_hidpp_device_ping(self, error)) {
		g_prefix_error_literal(error, "failed to ping: ");
		return FALSE;
	}

	/* did not get ID */
	if (self->device_idx == FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no HID++ ID");
		return FALSE;
	}

	/* add known root for HID++2.0 */
	g_ptr_array_set_size(self->feature_index, 0);
	if (self->hidpp_version >= 2.f) {
		FuLogitechHidppHidppMap *map = g_new0(FuLogitechHidppHidppMap, 1);
		map->idx = 0x00;
		map->feature = FU_LOGITECH_HIDPP_FEATURE_ROOT;
		g_ptr_array_add(self->feature_index, map);
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
	if (!fu_logitech_hidpp_device_fetch_model_id(self, error)) {
		g_prefix_error_literal(error, "failed to get the model ID: ");
		return FALSE;
	}

	/* try using HID++2.0 */
	idx = fu_logitech_hidpp_device_feature_get_idx(
	    self,
	    FU_LOGITECH_HIDPP_FEATURE_GET_DEVICE_NAME_TYPE);
	if (idx != 0x00) {
		const gchar *tmp;
		const guint8 *data;
		g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
		g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

		fu_struct_logitech_hidpp_msg_set_report_id(st_req,
							   FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
		fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
		fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
		fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x02 << 4); /* getDeviceType */

		st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
						    st_req,
						    self->hidpp_version,
						    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
						    error);
		if (st_rsp == NULL) {
			g_prefix_error_literal(error, "failed to get device type: ");
			return FALSE;
		}

		/* add nice-to-have data */
		data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
		tmp = fu_logitech_hidpp_device_get_summary(data[0]);
		if (tmp != NULL)
			fu_device_set_summary(FU_DEVICE(device), tmp);
		tmp = fu_logitech_hidpp_device_get_icon(data[0]);
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
		const guint8 *data;
		g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
		g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

		fu_struct_logitech_hidpp_msg_set_report_id(st_req,
							   FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
		fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
		fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
		fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x00 << 4); /* getDfuStatus */
		st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
						    st_req,
						    self->hidpp_version,
						    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
						    error);
		if (st_rsp == NULL) {
			g_prefix_error_literal(error, "failed to get DFU status: ");
			return FALSE;
		}
		data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, NULL);
		if ((data[2] & 0x01) > 0) {
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
	if (!fu_logitech_hidpp_device_fetch_firmware_info(self, error)) {
		g_prefix_error_literal(error, "failed to get firmware information: ");
		return FALSE;
	}

	/* get the battery level */
	if (!fu_logitech_hidpp_device_ensure_battery_level(self, error)) {
		g_prefix_error_literal(error, "failed to get battery level: ");
		return FALSE;
	}

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx != 0x00) {
		if (!fu_logitech_hidpp_device_rdfu_get_capabilities(self, error)) {
			g_prefix_error_literal(error, "failed to get RDFU capabilities: ");
			return FALSE;
		}
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.rdfu");
		fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LOGITECH_RDFU_FIRMWARE);
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	}

	/* poll for pings to track active state */
	fu_device_set_poll_interval(device, FU_LOGITECH_HIDPP_DEVICE_POLLING_INTERVAL);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	FuDevice *proxy;
	guint8 idx;

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

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* these may require user action */
	idx = fu_logitech_hidpp_device_feature_get_idx(self,
						       FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL_BOLT);
	if (idx == 0x00)
		idx =
		    fu_logitech_hidpp_device_feature_get_idx(self,
							     FU_LOGITECH_HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		FuDevice *parent;
		g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
		g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		g_autoptr(GError) error_local = NULL;
		const guint8 buf[] = {
		    0x01, /* enterDfu */
		    0x00, /* dfuControlParam */
		    0x00, /* unused */
		    0x00, /* unused */
		    'D',
		    'F',
		    'U',
		};

		fu_struct_logitech_hidpp_msg_set_report_id(st_req,
							   FU_LOGITECH_HIDPP_REPORT_ID_LONG);
		fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
		fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
		fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x01 << 4); /* setDfuControl */
		if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
			return FALSE;
		st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
						    st_req,
						    self->hidpp_version,
						    FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
							FU_LOGITECH_HIDPP_MSG_FLAG_NON_BLOCKING_IO,
						    &error_local);
		if (st_rsp == NULL) {
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
		parent = fu_device_get_parent(device, NULL);
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
		g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();

		const guint8 buf[] = {
		    0x01, /* startDfu */
		    0x00, /* dfuControlParam */
		    0x00, /* unused */
		    0x00, /* unused */
		    'D',
		    'F',
		    'U',
		};
		g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;

		fu_struct_logitech_hidpp_msg_set_report_id(st_req,
							   FU_LOGITECH_HIDPP_REPORT_ID_LONG);
		fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
		fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
		fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x01 << 4); /* setDfuControl */
		if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
			return FALSE;
		st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
						    st_req,
						    self->hidpp_version,
						    FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_SUB_ID,
						    error);
		if (st_rsp == NULL) {
			g_prefix_error_literal(error, "failed to put device into DFU mode: ");
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
					    const guint8 *buf,
					    gsize bufsz,
					    GError **error)
{
	FuLogitechHidppMsgFlags flags = FU_LOGITECH_HIDPP_MSG_FLAG_NONE;
	FuDevice *proxy;
	const guint8 *data;
	gsize datasz = 0;
	guint32 packet_cnt;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* enable transfer workaround for devices paired to Bolt receiver */
	if (self->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    self->device_idx != FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER)
		flags = FU_LOGITECH_HIDPP_MSG_FLAG_RETRY_STUCK;

	/* send firmware data */
	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_LONG);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
	fu_struct_logitech_hidpp_msg_set_function_id(st_req, cmd << 4); /* dfuStart|dfuCmdDataX */
	if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, bufsz, error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(proxy),
					    st_req,
					    self->hidpp_version,
					    flags,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to supply program data: ");
		return FALSE;
	}

	/* check error */
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &datasz);
	if (!fu_memread_uint32_safe(data, datasz, 0x0, &packet_cnt, G_BIG_ENDIAN, error))
		return FALSE;
	g_debug("packet_cnt=0x%04x", packet_cnt);
	if (fu_logitech_hidpp_device_check_status(data[4], &error_local))
		return TRUE;

	/* fatal error */
	if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BUSY)) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* wait for the HID++ notification */
	g_debug("ignoring: %s", error_local->message);
	for (guint retry = 0; retry < 10; retry++) {
		g_autoptr(FuStructLogitechHidppMsg) st_rsp2 = NULL;

		st_rsp2 = fu_logitech_hidpp_receive(FU_UDEV_DEVICE(proxy), 15000, error);
		if (st_rsp2 == NULL)
			return FALSE;
		data = fu_struct_logitech_hidpp_msg_get_data(st_rsp2, NULL);
		if (fu_logitech_hidpp_msg_is_reply(st_req,
						   st_rsp2,
						   FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_FNCT_ID)) {
			g_autoptr(GError) error2 = NULL;
			if (!fu_logitech_hidpp_device_check_status(data[4], &error2)) {
				g_debug("got %s, waiting a bit longer", error2->message);
				continue;
			}
			return TRUE;
		}
		g_debug("got wrong packet, continue to wait…");
	}

	/* nothing in the queue */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "failed to get event after timeout");
	return FALSE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_dfu_img(FuLogitechHidppDevice *self,
						FuFirmware *firmware,
						FuProgress *progress,
						GError **error)
{
	guint8 cmd = 0x04;
	guint8 fw_entity = 0;
	guint8 idx;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no DFU feature available");
		return FALSE;
	}

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream, 0x0, 0x0, 16, error);
	if (chunks == NULL)
		return FALSE;

	/* flash hardware */
	if (!fu_input_stream_read_u8(stream, 0x0, &fw_entity, error))
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (gsize i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* send packet and wait for reply */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_logitech_hidpp_device_write_firmware_pkt(self,
								 idx,
								 cmd,
								 fu_chunk_get_data(chk),
								 fu_chunk_get_data_sz(chk),
								 error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x for entity 0x%x: ",
				       (guint)fu_chunk_get_address(chk),
				       fw_entity);
			return FALSE;
		}

		/* use sliding window */
		cmd = (cmd + 1) % 4;

		/* update progress-bar */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_dfu(FuLogitechHidppDevice *self,
					    FuFirmware *firmware,
					    FuProgress *progress,
					    FwupdInstallFlags flags,
					    GError **error)
{
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* write each image in the zip archive */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_logitech_hidpp_device_write_firmware_dfu_img(
			self,
			img,
			fu_progress_get_child(progress),
			error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_rdfu_entity(FuLogitechHidppDevice *self,
						    FuLogitechRdfuEntity *entity_fw,
						    FuProgress *progress,
						    FwupdInstallFlags flags,
						    GError **error)
{
	guint retry = 0;
	GPtrArray *blocks = fu_logitech_rdfu_entity_get_blocks(entity_fw);
	g_autoptr(GError) error_local = NULL;

	/* verify model */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0 &&
	    g_strcmp0(self->model_id, fu_logitech_rdfu_entity_get_model_id(entity_fw)) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware for model %s, but the target is %s",
			    fu_logitech_rdfu_entity_get_model_id(entity_fw),
			    self->model_id);
		return FALSE;
	}

	/* check if we in update mode already */
	if (!fu_logitech_hidpp_device_rdfu_get_dfu_status(self, &error_local)) {
		if (error_local->message != NULL)
			g_debug("forcing startDFU, reason %s", error_local->message);
		/* try to drop the inner state at device */
		fu_logitech_hidpp_device_rdfu_set_state(self,
							FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
	}

	/* device requested to start or restart for some reason */
	if (self->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED) {
		if (!fu_logitech_hidpp_device_rdfu_start_dfu(
			self,
			fu_logitech_rdfu_entity_get_magic(entity_fw),
			error))
			return FALSE;
	}

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_id(progress, G_STRLOC);
	while (self->rdfu_state == FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER) {
		/* update progress-bar here to avoid jumps caused dfu-transfer-complete */
		fu_progress_set_percentage_full(progress, self->rdfu_block, blocks->len);

		/* send packet and wait for reply */
		if (!fu_logitech_hidpp_device_rdfu_transfer_data(self, blocks, error))
			return FALSE;

		/* additional protection from misbehaviored devices */
		if (self->rdfu_state != FU_LOGITECH_HIDPP_RDFU_STATE_TRANSFER) {
			if (!fu_logitech_hidpp_device_rdfu_get_dfu_status(self, error))
				return FALSE;

			/* too many soft restarts, let's fail everything */
			if (retry++ > FU_LOGITECH_HIDPP_DEVICE_RDFU_MAX_RETRIES) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_WRITE,
						    "too lot recover attempts");
				return FALSE;
			}
		}
	}

	g_debug("RDFU supported, applying the update");
	if (!fu_logitech_hidpp_device_rdfu_apply_dfu(self, self->cached_fw_entity, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_write_firmware_rdfu(FuLogitechHidppDevice *self,
					     FuFirmware *firmware,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	guint8 idx;
	g_autoptr(FuFirmware) entity_fw = NULL;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no RDFU feature available");
		return FALSE;
	}

	entity_fw = fu_firmware_get_image_by_idx(firmware, self->cached_fw_entity, error);
	if (entity_fw == NULL)
		return FALSE;
	return fu_logitech_hidpp_device_write_firmware_rdfu_entity(
	    self,
	    FU_LOGITECH_RDFU_ENTITY(entity_fw),
	    progress,
	    flags,
	    error);
}

static gboolean
fu_logitech_hidpp_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	guint8 idx;

	/* device should support either RDFU or DFU mode */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx != 0x00) {
		return fu_logitech_hidpp_device_write_firmware_rdfu(self,
								    firmware,
								    progress,
								    flags,
								    error);
	}

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
	if (idx != 0x00) {
		return fu_logitech_hidpp_device_write_firmware_dfu(self,
								   firmware,
								   progress,
								   flags,
								   error);
	}

	g_set_error_literal(error,
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

static gboolean
fu_logitech_hidpp_device_attach_cached(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	FuDevice *proxy;
	guint8 idx;

	/* get current proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_RDFU);
	if (idx == 0x00) {
		const guint8 buf[] = {self->cached_fw_entity}; /* fwEntity */
		g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
		g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
		g_autoptr(GError) error_local = NULL;

		/* sanity check for DFU*/
		if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
			g_debug("already in runtime mode, skipping");
			return TRUE;
		}

		/* if we're in bootloader mode, we should be able to get this feature */
		idx = fu_logitech_hidpp_device_feature_get_idx(self, FU_LOGITECH_HIDPP_FEATURE_DFU);
		if (idx == 0x00) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no DFU feature available");
			return FALSE;
		}

		/* reboot back into firmware mode */
		fu_struct_logitech_hidpp_msg_set_report_id(st_req,
							   FU_LOGITECH_HIDPP_REPORT_ID_LONG);
		fu_struct_logitech_hidpp_msg_set_device_id(st_req, self->device_idx);
		fu_struct_logitech_hidpp_msg_set_sub_id(st_req, idx);
		fu_struct_logitech_hidpp_msg_set_function_id(st_req, 0x05 << 4); /* restart */
		if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
			return FALSE;
		st_rsp = fu_logitech_hidpp_transfer(
		    FU_UDEV_DEVICE(proxy),
		    st_req,
		    self->hidpp_version,
		    FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_SUB_ID |
			FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_SWID | /* inferred? */
			FU_LOGITECH_HIDPP_MSG_FLAG_NON_BLOCKING_IO,
		    &error_local);
		if (st_rsp == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE) ||
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

	/* device will reset? */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH)) {
		fu_device_set_poll_interval(FU_DEVICE(self), 0);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	} else {
		/* device file hasn't been unbound/re-bound, just probe again */
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_logitech_hidpp_device_reprobe_cb,
				     10,
				     NULL,
				     error))
			return FALSE;
	}

	/* success */
	return TRUE;


}

static gboolean
fu_logitech_hidpp_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
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
fu_logitech_hidpp_device_set_progress(FuDevice *device, FuProgress *progress)
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
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(object);
	g_ptr_array_unref(self->feature_index);
	g_free(self->model_id);
	G_OBJECT_CLASS(fu_logitech_hidpp_device_parent_class)->finalize(object);
}

static void
fu_logitech_hidpp_device_constructed(GObject *object)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(object);

	g_signal_connect(FU_DEVICE(self),
			 "notify::proxy",
			 G_CALLBACK(fu_logitech_hidpp_device_proxy_notify_cb),
			 NULL);
	G_OBJECT_CLASS(fu_logitech_hidpp_device_parent_class)->constructed(object);
}

static gboolean
fu_logitech_hidpp_device_cleanup(FuDevice *device,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuDevice *parent = fu_device_get_parent(device, NULL);
	if (parent != NULL) {
		fu_device_set_poll_interval(parent,
					    FU_LOGITECH_HIDPP_RECEIVER_RUNTIME_POLLING_INTERVAL);
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_device_from_json(FuDevice *device, FwupdJsonObject *json_obj, GError **error)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);
	const gchar *tmp;
	gint64 tmp64 = 0;

	/* name */
	tmp = fwupd_json_object_get_string(json_obj, "Name", error);
	if (tmp == NULL)
		return FALSE;
	fu_device_set_name(device, tmp);

	/* idx */
	if (!fwupd_json_object_get_integer(json_obj, "DeviceIdx", &tmp64, error))
		return FALSE;
	fu_logitech_hidpp_device_set_device_idx(self, tmp64);

	/* HID++ pid */
	if (!fwupd_json_object_get_integer(json_obj, "HidppPid", &tmp64, error))
		return FALSE;
	fu_logitech_hidpp_device_set_hidpp_pid(self, tmp64);

	/* success */
	return TRUE;
}

static void
fu_logitech_hidpp_device_add_json(FuDevice *device,
				  FwupdJsonObject *json_obj,
				  FwupdCodecFlags flags)
{
	FuLogitechHidppDevice *self = FU_LOGITECH_HIDPP_DEVICE(device);

	/* properties */
	fwupd_json_object_add_string(json_obj,
				     "GType",
				     g_type_name(fu_device_get_proxy_gtype(device)));
	fwupd_json_object_add_string(json_obj, "Name", fu_device_get_name(device));
	fwupd_json_object_add_integer(json_obj, "DeviceIdx", self->device_idx);
	fwupd_json_object_add_integer(json_obj, "HidppPid", self->hidpp_pid);
}

static void
fu_logitech_hidpp_device_class_init(FuLogitechHidppDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_logitech_hidpp_device_constructed;
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
	device_class->from_json = fu_logitech_hidpp_device_from_json;
	device_class->add_json = fu_logitech_hidpp_device_add_json;
}

static void
fu_logitech_hidpp_device_init(FuLogitechHidppDevice *self)
{
	self->device_idx = FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED;
	self->feature_index = g_ptr_array_new_with_free_func(g_free);
	fu_logitech_hidpp_device_rdfu_set_state(self, FU_LOGITECH_HIDPP_RDFU_STATE_NOT_STARTED);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_vid(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_VID);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_UDEV_DEVICE);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_FORCE_RECEIVER_ID);
	fu_device_register_private_flag(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_FLAG_BLE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_REBIND_ATTACH);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_DEVICE_FLAG_NO_REQUEST_REQUIRED);
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
fu_logitech_hidpp_device_new(FuUdevDevice *proxy)
{
	return g_object_new(FU_TYPE_LOGITECH_HIDPP_DEVICE,
			    "proxy",
			    proxy,
			    NULL);
}
