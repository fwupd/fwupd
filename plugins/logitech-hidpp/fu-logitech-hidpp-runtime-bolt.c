/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-runtime-bolt.h"
#include "fu-logitech-hidpp-struct.h"

struct _FuLogitechHidppRuntimeBolt {
	FuLogitechHidppRuntime parent_instance;
	guint8 pairing_slots;
};

G_DEFINE_TYPE(FuLogitechHidppRuntimeBolt,
	      fu_logitech_hidpp_runtime_bolt,
	      FU_TYPE_LOGITECH_HIDPP_RUNTIME)

static gboolean
fu_logitech_hidpp_runtime_bolt_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppRuntime *self = FU_LOGITECH_HIDPP_RUNTIME(device);
	g_autoptr(FuStructLogitechHidppMsg) st = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	g_autoptr(GError) error_local = NULL;
	const guint8 buf[] = {
	    0x01, /* enable DFU */
	    0x00,
	    0x00,
	    0x00,
	    'P',
	    'R',
	    'E',
	};

	fu_struct_logitech_hidpp_msg_set_report_id(st, FU_LOGITECH_HIDPP_REPORT_ID_LONG);
	fu_struct_logitech_hidpp_msg_set_device_id(st, FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER);
	fu_struct_logitech_hidpp_msg_set_sub_id(st, FU_LOGITECH_HIDPP_SUBID_SET_LONG_REGISTER);
	fu_struct_logitech_hidpp_msg_set_function_id(st,
						     FU_LOGITECH_HIDPP_BOLT_REGISTER_DFU_CONTROL);
	if (!fu_struct_logitech_hidpp_msg_set_data(st, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_logitech_hidpp_send(FU_UDEV_DEVICE(self),
				    st,
				    FU_LOGITECH_HIDPP_VERSION_1,
				    FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS * 10,
				    FU_LOGITECH_HIDPP_MSG_FLAG_NON_BLOCKING_IO,
				    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("failed to detach to bootloader: %s", error_local->message);
		} else {
			g_prefix_error_literal(&error_local, "failed to detach to bootloader: ");
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* the user has to do something for Logitech "security" reasons */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	if (!fu_device_emit_request(device, request, progress, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_logitech_hidpp_runtime_bolt_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidppRuntimeBolt *self = FU_LOGITECH_HIDPP_RUNTIME_BOLT(device);
	fwupd_codec_string_append_int(str, idt, "PairingSlots", self->pairing_slots);
}

static FuLogitechHidppDevice *
fu_logitech_hidpp_runtime_bolt_find_paired_device(FuLogitechHidppRuntimeBolt *self,
						  guint16 hidpp_pid)
{
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));

	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (FU_IS_LOGITECH_HIDPP_DEVICE(child) &&
		    fu_logitech_hidpp_device_get_hidpp_pid(FU_LOGITECH_HIDPP_DEVICE(child)) ==
			hidpp_pid)
			return FU_LOGITECH_HIDPP_DEVICE(g_object_ref(child));
	}

	return NULL;
}

static gchar *
fu_logitech_hidpp_runtime_bolt_query_device_name(FuLogitechHidppRuntime *self,
						 guint8 slot,
						 GError **error)
{
	g_autoptr(FuStructLogitechHidppMsg) st = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	const guint8 *data;
	gsize datasz = 0;
	guint8 namelen = 0;
	const guint8 buf[] = {
	    0x60 | slot, /* device name */
	    0x01,
	};

	fu_struct_logitech_hidpp_msg_set_report_id(st, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st, FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER);
	fu_struct_logitech_hidpp_msg_set_sub_id(st, FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER);
	fu_struct_logitech_hidpp_msg_set_function_id(
	    st,
	    FU_LOGITECH_HIDPP_BOLT_REGISTER_PAIRING_INFORMATION);
	if (!fu_struct_logitech_hidpp_msg_set_data(st, buf, sizeof(buf), error))
		return NULL;

	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self),
					    st,
					    FU_LOGITECH_HIDPP_VERSION_1,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error(error, "failed to retrieve the device name for slot %d: ", slot);
		return NULL;
	}
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &datasz);
	if (!fu_memread_uint8_safe(data, datasz, 0x2, &namelen, error))
		return NULL;
	return fu_memstrsafe(data, datasz, 0x3, namelen, error);
}

static gboolean
fu_logitech_hidpp_runtime_bolt_update_paired_device(FuLogitechHidppRuntimeBolt *self,
						    FuStructLogitechHidppMsg *st,
						    GError **error)
{
	FuLogitechHidppRuntime *runtime = FU_LOGITECH_HIDPP_RUNTIME(self);
	const guint8 *data;
	gboolean reachable = FALSE;
	gsize datasz = 0;
	guint16 hidpp_pid;
	guint8 flags = 0;
	g_autoptr(FuLogitechHidppDevice) child = NULL;

	data = fu_struct_logitech_hidpp_msg_get_data(st, &datasz);
	if (!fu_memread_uint8_safe(data, datasz, 0x0, &flags, error))
		return FALSE;
	if (!fu_memread_uint16_safe(data, datasz, 0x1, &hidpp_pid, G_BIG_ENDIAN, error))
		return FALSE;
	if ((flags & 0x40) == 0)
		reachable = TRUE;

	child = fu_logitech_hidpp_runtime_bolt_find_paired_device(self, hidpp_pid);
	if (child != NULL) {
		g_autofree gchar *id_display = fu_device_get_id_display(FU_DEVICE(child));
		g_debug("%s is reachable:%i", id_display, reachable);
		if (reachable) {
			g_autoptr(FuDeviceLocker) locker = NULL;

			/* known paired & reachable */
			fu_device_probe_invalidate(FU_DEVICE(child));
			locker = fu_device_locker_new(FU_DEVICE(child), error);
			if (locker == NULL) {
				g_prefix_error_literal(error, "cannot rescan paired device: ");
				return FALSE;
			}
			fu_device_remove_flag(FU_DEVICE(child), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		} else {
			/* any successful 'ping' will clear this */
			fu_device_add_flag(FU_DEVICE(child), FWUPD_DEVICE_FLAG_UNREACHABLE);
		}
	} else if (reachable) {
		g_autofree gchar *name = NULL;

		/* unknown paired device, reachable state */
		name = fu_logitech_hidpp_runtime_bolt_query_device_name(
		    runtime,
		    fu_struct_logitech_hidpp_msg_get_device_id(st),
		    error);
		if (name == NULL)
			return FALSE;
		child = fu_logitech_hidpp_device_new(FU_UDEV_DEVICE(self));
		fu_device_set_name(FU_DEVICE(child), name);
		fu_logitech_hidpp_device_set_device_idx(
		    child,
		    fu_struct_logitech_hidpp_msg_get_device_id(st));
		fu_logitech_hidpp_device_set_hidpp_pid(child, hidpp_pid);
		if (!fu_device_open(FU_DEVICE(child), error))
			return FALSE;
		if (!fu_device_probe(FU_DEVICE(child), error))
			return FALSE;
		if (!fu_device_setup(FU_DEVICE(child), error))
			return FALSE;
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));
	} else {
		/* unknown paired device, unreachable state */
		g_warning("unknown paired device 0x%0x in slot %u (unreachable)",
			  hidpp_pid,
			  fu_struct_logitech_hidpp_msg_get_device_id(st));
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_poll_peripheral(FuLogitechHidppRuntime *self,
					       guint i,
					       GError **error)
{
	const guint8 *data;
	gsize datasz = 0;
	guint16 hidpp_pid = 0;
	guint8 flags = 0;
	g_autofree gchar *name = NULL;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	const guint8 buf[] = {0x50 | i}; /* pairing information */

	name = fu_logitech_hidpp_runtime_bolt_query_device_name(self, i, error);
	if (name == NULL)
		return FALSE;

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER);
	fu_struct_logitech_hidpp_msg_set_function_id(
	    st_req,
	    FU_LOGITECH_HIDPP_BOLT_REGISTER_PAIRING_INFORMATION);
	if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self),
					    st_req,
					    FU_LOGITECH_HIDPP_VERSION_1,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL)
		return FALSE;
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &datasz);
	if (!fu_memread_uint8_safe(data, datasz, 0x1, &flags, error))
		return FALSE;
	if (!fu_memread_uint16_safe(data, datasz, 0x2, &hidpp_pid, G_BIG_ENDIAN, error))
		return FALSE;
	if ((flags & 0x40) == 0) {
		/* paired device is reachable */
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(FuLogitechHidppDevice) child = NULL;

		child = fu_logitech_hidpp_device_new(FU_UDEV_DEVICE(self));
		fu_device_set_proxy_gtype(FU_DEVICE(child), FU_TYPE_UDEV_DEVICE);
		fu_device_set_install_duration(FU_DEVICE(child), 270);
		fu_device_set_name(FU_DEVICE(child), name);
		fu_logitech_hidpp_device_set_device_idx(child, i);
		fu_logitech_hidpp_device_set_hidpp_pid(child, hidpp_pid);

		locker = fu_device_locker_new(FU_DEVICE(child), error);
		if (locker == NULL) {
			g_prefix_error(error, "failed to probe %s: ", name);
			return FALSE;
		}
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));
	}

	/* success */
	return TRUE;
}

static void
fu_logitech_hidpp_runtime_bolt_poll_peripherals(FuLogitechHidppRuntime *self)
{
	FuLogitechHidppRuntimeBolt *bolt = FU_LOGITECH_HIDPP_RUNTIME_BOLT(self);
	for (guint i = 1; i <= bolt->pairing_slots; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_logitech_hidpp_runtime_bolt_poll_peripheral(self, i, &error_local)) {
			g_debug("cannot poll slot %u: %s", i, error_local->message);
			continue;
		}
	}
}

static gboolean
fu_logitech_hidpp_runtime_bolt_process_notification(FuLogitechHidppRuntimeBolt *self,
						    FuStructLogitechHidppMsg *st)
{
	g_autoptr(GError) error_local = NULL;

	/* HID++1.0 error */
	if (!fu_logitech_hidpp_msg_is_error(st, &error_local)) {
		g_warning("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* unifying receiver notification */
	if (fu_struct_logitech_hidpp_msg_get_report_id(st) == FU_LOGITECH_HIDPP_REPORT_ID_SHORT) {
		switch (fu_struct_logitech_hidpp_msg_get_sub_id(st)) {
		case FU_LOGITECH_HIDPP_SUBID_DEVICE_CONNECTION:
		case FU_LOGITECH_HIDPP_SUBID_DEVICE_DISCONNECTION:
		case FU_LOGITECH_HIDPP_SUBID_DEVICE_LOCKING_CHANGED:
			if (!fu_logitech_hidpp_runtime_bolt_update_paired_device(self,
										 st,
										 &error_local)) {
				g_warning("failed to update paired device status: %s",
					  error_local->message);
				return FALSE;
			}
			break;
		case FU_LOGITECH_HIDPP_SUBID_LINK_QUALITY:
			g_debug("ignoring link quality message");
			break;
		case FU_LOGITECH_HIDPP_SUBID_ERROR_MSG:
			g_debug("ignoring error message");
			break;
		default:
			g_debug("unknown SubID %02x", fu_struct_logitech_hidpp_msg_get_sub_id(st));
			break;
		}
	}

	return TRUE;
}

static FuStructLogitechHidppMsg *
fu_logitech_hidpp_runtime_bolt_find_newest_msg(GPtrArray *st_rsps, guint8 device_id, guint8 sub_id)
{
	for (guint i = 0; i < st_rsps->len; i++) {
		FuStructLogitechHidppMsg *st = g_ptr_array_index(st_rsps, st_rsps->len - (i + 1));
		if (fu_struct_logitech_hidpp_msg_get_device_id(st) == device_id &&
		    fu_struct_logitech_hidpp_msg_get_sub_id(st) == sub_id)
			return st;
	}
	return NULL;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_poll(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *runtime = FU_LOGITECH_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimeBolt *self = FU_LOGITECH_HIDPP_RUNTIME_BOLT(device);
	const guint timeout = 1; /* ms */
	g_autoptr(GPtrArray) st_rsps =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_struct_logitech_hidpp_msg_unref);

	/* not predictable for time */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* open -- not a locker as we have no kernel driver */
	if (!fu_device_open(device, error))
		return FALSE;

	/* drain all the pending messages into the array */
	for (guint i = 0; i < 50; i++) {
		g_autoptr(FuStructLogitechHidppMsg) st = NULL;
		g_autoptr(GError) error_local = NULL;

		st = fu_logitech_hidpp_receive(FU_UDEV_DEVICE(runtime), timeout, &error_local);
		if (st == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT))
				break;
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("ignoring: %s", error_local->message);
				break;
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "error polling Bolt receiver: ");
			return FALSE;
		}
		g_ptr_array_add(st_rsps, g_steal_pointer(&st));
	}

	/* process messages in order, but discard any message with a newer version */
	for (guint i = 0; i < st_rsps->len; i++) {
		FuStructLogitechHidppMsg *st = g_ptr_array_index(st_rsps, i);
		FuStructLogitechHidppMsg *st_newest;

		/* find the newest message with the matching device and sub-IDs */
		st_newest = fu_logitech_hidpp_runtime_bolt_find_newest_msg(
		    st_rsps,
		    fu_struct_logitech_hidpp_msg_get_device_id(st),
		    fu_struct_logitech_hidpp_msg_get_sub_id(st));
		if (st != st_newest) {
			g_debug("ignoring duplicate message device-id:%02x [%s] sub-id:%02x [%s]",
				fu_struct_logitech_hidpp_msg_get_device_id(st),
				fu_logitech_hidpp_device_idx_to_string(
				    fu_struct_logitech_hidpp_msg_get_device_id(st)),
				fu_struct_logitech_hidpp_msg_get_sub_id(st),
				fu_logitech_hidpp_subid_to_string(
				    fu_struct_logitech_hidpp_msg_get_sub_id(st)));
			continue;
		}
		fu_logitech_hidpp_runtime_bolt_process_notification(self, st);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_setup_slot(FuLogitechHidppRuntime *self, guint i, GError **error)
{
	const guint8 *data;
	gsize datasz = 0;
	guint16 version_raw = 0;
	guint8 fw_type = 0;
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppMsg) st_req = fu_struct_logitech_hidpp_msg_new();
	const guint8 buf[] = {i};

	fu_struct_logitech_hidpp_msg_set_report_id(st_req, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st_req, FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER);
	fu_struct_logitech_hidpp_msg_set_sub_id(st_req, FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER);
	fu_struct_logitech_hidpp_msg_set_function_id(
	    st_req,
	    FU_LOGITECH_HIDPP_BOLT_REGISTER_RECEIVER_FW_INFORMATION);
	if (!fu_struct_logitech_hidpp_msg_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self),
					    st_req,
					    FU_LOGITECH_HIDPP_VERSION_1,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to read device config: ");
		return FALSE;
	}

	/* main application */
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &datasz);
	if (!fu_memread_uint8_safe(data, datasz, 0x0, &fw_type, error))
		return FALSE;
	if (fw_type == 0) {
		guint8 vmaj = 0;
		guint8 vmin = 0;
		g_autofree gchar *version = NULL;

		if (!fu_memread_uint8_safe(data, datasz, 0x1, &vmaj, error))
			return FALSE;
		if (!fu_memread_uint8_safe(data, datasz, 0x2, &vmin, error))
			return FALSE;
		if (!fu_memread_uint16_safe(data, datasz, 0x03, &version_raw, G_BIG_ENDIAN, error))
			return FALSE;
		version = fu_logitech_hidpp_format_version("MPR", vmaj, vmin, version_raw);
		fu_device_set_version(FU_DEVICE(self), version);
		return TRUE;
	}

	/* bootloader */
	if (fw_type == 1) {
		g_autofree gchar *version = NULL;
		guint8 vmaj = 0;
		guint8 vmin = 0;

		if (!fu_memread_uint8_safe(data, datasz, 0x1, &vmaj, error))
			return FALSE;
		if (!fu_memread_uint8_safe(data, datasz, 0x2, &vmin, error))
			return FALSE;
		if (!fu_memread_uint16_safe(data, datasz, 0x03, &version_raw, G_BIG_ENDIAN, error))
			return FALSE;
		version = fu_logitech_hidpp_format_version("BOT", vmaj, vmin, version_raw);
		fu_device_set_version_bootloader(FU_DEVICE(self), version);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_setup_internal(FuLogitechHidppRuntime *self, GError **error)
{
	FuLogitechHidppRuntimeBolt *bolt = FU_LOGITECH_HIDPP_RUNTIME_BOLT(self);
	g_autoptr(FuStructLogitechHidppMsg) st = fu_struct_logitech_hidpp_msg_new();
	g_autoptr(FuStructLogitechHidppMsg) st_rsp = NULL;
	const guint8 *data;
	const guint8 buf[] = {0x02}; /* FW Version (contains the number of pairing slots) */
	gsize datasz = 0;

	fu_struct_logitech_hidpp_msg_set_report_id(st, FU_LOGITECH_HIDPP_REPORT_ID_SHORT);
	fu_struct_logitech_hidpp_msg_set_device_id(st, FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER);
	fu_struct_logitech_hidpp_msg_set_sub_id(st, FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER);
	fu_struct_logitech_hidpp_msg_set_function_id(
	    st,
	    FU_LOGITECH_HIDPP_BOLT_REGISTER_PAIRING_INFORMATION);
	if (!fu_struct_logitech_hidpp_msg_set_data(st, buf, sizeof(buf), error))
		return FALSE;

	st_rsp = fu_logitech_hidpp_transfer(FU_UDEV_DEVICE(self),
					    st,
					    FU_LOGITECH_HIDPP_VERSION_1,
					    FU_LOGITECH_HIDPP_MSG_FLAG_NONE,
					    error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to fetch the number of pairing slots: ");
		return FALSE;
	}
	data = fu_struct_logitech_hidpp_msg_get_data(st_rsp, &datasz);
	if (!fu_memread_uint8_safe(data, datasz, 0x8, &bolt->pairing_slots, error))
		return FALSE;

	/*
	 * TODO: Iterate only over the first three entity indexes for
	 * now.
	 */
	for (guint i = 0; i < 3; i++) {
		if (!fu_logitech_hidpp_runtime_bolt_setup_slot(self, i, error))
			return FALSE;
	}

	/* enable HID++ notifications */
	if (!fu_logitech_hidpp_runtime_enable_notifications(self, error)) {
		g_prefix_error_literal(error, "failed to enable notifications: ");
		return FALSE;
	}
	fu_logitech_hidpp_runtime_bolt_poll_peripherals(self);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_setup(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *self = FU_LOGITECH_HIDPP_RUNTIME(device);
	g_autoptr(GError) error_local = NULL;
	for (guint i = 0; i < 5; i++) {
		g_clear_error(&error_local);
		/* HID++1.0 devices have to sleep to allow Solaar to talk to
		 * the device first -- we can't use the SwID as this is a
		 * HID++2.0 feature */
		fu_device_sleep(device, 200); /* ms */
		if (fu_logitech_hidpp_runtime_bolt_setup_internal(self, &error_local))
			return TRUE;
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	g_propagate_error(error, g_steal_pointer(&error_local));
	return FALSE;
}

static void
fu_logitech_hidpp_runtime_bolt_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 16, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 84, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_logitech_hidpp_runtime_bolt_class_init(FuLogitechHidppRuntimeBoltClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_logitech_hidpp_runtime_bolt_detach;
	device_class->setup = fu_logitech_hidpp_runtime_bolt_setup;
	device_class->poll = fu_logitech_hidpp_runtime_bolt_poll;
	device_class->to_string = fu_logitech_hidpp_runtime_bolt_to_string;
	device_class->set_progress = fu_logitech_hidpp_runtime_bolt_set_progress;
}

static void
fu_logitech_hidpp_runtime_bolt_init(FuLogitechHidppRuntimeBolt *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_name(FU_DEVICE(self), "Bolt Receiver");
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ZIP_FIRMWARE);
}
