/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
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

struct _FuLogitechHidppRuntimeBolt {
	FuLogitechHidppRuntime parent_instance;
	guint8 pairing_slots;
};

G_DEFINE_TYPE(FuLogitechHidppRuntimeBolt, fu_logitech_hidpp_runtime_bolt, FU_TYPE_HIDPP_RUNTIME)

static gboolean
fu_logitech_hidpp_runtime_bolt_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;

	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_LONG;
	msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
	msg->sub_id = FU_LOGITECH_HIDPP_SUBID_SET_LONG_REGISTER;
	msg->function_id = BOLT_REGISTER_DFU_CONTROL;
	msg->data[0] = 1; /* Enable DFU */
	msg->data[4] = 'P';
	msg->data[5] = 'R';
	msg->data[6] = 'E';
	msg->hidpp_version = 1;
	msg->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_send(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
				    msg,
				    FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
				    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE)) {
			g_debug("failed to detach to bootloader: %s", error_local->message);
		} else {
			g_prefix_error(&error_local, "failed to detach to bootloader: ");
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_logitech_hidpp_runtime_bolt_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidppRuntimeBolt *self = FU_HIDPP_RUNTIME_BOLT(device);
	fwupd_codec_string_append_int(str, idt, "PairingSlots", self->pairing_slots);
}

static FuLogitechHidppDevice *
fu_logitech_hidpp_runtime_bolt_find_paired_device(FuDevice *device, guint16 hidpp_pid)
{
	GPtrArray *children = fu_device_get_children(device);

	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (FU_IS_HIDPP_DEVICE(child) &&
		    fu_logitech_hidpp_device_get_hidpp_pid(FU_HIDPP_DEVICE(child)) == hidpp_pid)
			return FU_HIDPP_DEVICE(g_object_ref(child));
	}

	return NULL;
}

static gchar *
fu_logitech_hidpp_runtime_bolt_query_device_name(FuLogitechHidppRuntime *self,
						 guint8 slot,
						 GError **error)
{
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GString) dev_name = g_string_new(NULL);
	guint namelen;

	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
	msg->sub_id = FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER;
	msg->function_id = BOLT_REGISTER_PAIRING_INFORMATION;
	msg->data[0] = 0x60 | slot; /* device name */
	msg->data[1] = 1;
	msg->hidpp_version = 1;
	if (!fu_logitech_hidpp_transfer(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
					msg,
					error)) {
		g_prefix_error(error, "failed to retrieve the device name for slot %d: ", slot);
		return NULL;
	}
	namelen = msg->data[2];
	g_string_append_len(dev_name, (const char *)(&(msg->data[3])), namelen);

	return g_string_free(g_steal_pointer(&dev_name), FALSE);
}

static gboolean
fu_logitech_hidpp_runtime_bolt_update_paired_device(FuLogitechHidppRuntimeBolt *self,
						    FuLogitechHidppHidppMsg *msg,
						    GError **error)
{
	FuLogitechHidppRuntime *runtime = FU_HIDPP_RUNTIME(self);
	gboolean reachable = FALSE;
	guint16 hidpp_pid;
	g_autoptr(FuLogitechHidppDevice) child = NULL;

	if ((msg->data[0] & 0x40) == 0)
		reachable = TRUE;
	hidpp_pid = (msg->data[1] << 8) | msg->data[2];

	child = fu_logitech_hidpp_runtime_bolt_find_paired_device(FU_DEVICE(self), hidpp_pid);
	if (child != NULL) {
		g_debug("%s [%s] is reachable:%i",
			fu_device_get_name(FU_DEVICE(child)),
			fu_device_get_name(FU_DEVICE(child)),
			reachable);
		if (reachable) {
			g_autoptr(FuDeviceLocker) locker = NULL;

			/* known paired & reachable */
			fu_device_probe_invalidate(FU_DEVICE(child));
			locker = fu_device_locker_new(FU_DEVICE(child), error);
			if (locker == NULL) {
				g_prefix_error(error, "cannot rescan paired device: ");
				return FALSE;
			}
			fu_device_remove_flag(FU_DEVICE(child), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		} else {
			GPtrArray *children = NULL;
			/* any successful 'ping' will clear this */
			fu_device_add_flag(FU_DEVICE(child), FWUPD_DEVICE_FLAG_UNREACHABLE);
			children = fu_device_get_children(FU_DEVICE(child));
			for (guint i = 0; i < children->len; i++) {
				FuDevice *radio = g_ptr_array_index(children, i);
				fu_device_add_flag(radio, FWUPD_DEVICE_FLAG_UNREACHABLE);
			}
		}
	} else if (reachable) {
		g_autofree gchar *name = NULL;

		/* unknown paired device, reachable state */
		name = fu_logitech_hidpp_runtime_bolt_query_device_name(runtime,
									msg->device_id,
									error);
		if (name == NULL)
			return FALSE;
		child = fu_logitech_hidpp_device_new(FU_UDEV_DEVICE(self));
		fu_device_set_name(FU_DEVICE(child), name);
		fu_logitech_hidpp_device_set_device_idx(child, msg->device_id);
		fu_logitech_hidpp_device_set_hidpp_pid(child, hidpp_pid);
		if (!fu_device_probe(FU_DEVICE(child), error))
			return FALSE;
		if (!fu_device_setup(FU_DEVICE(child), error))
			return FALSE;
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));
	} else {
		/* unknown paired device, unreachable state */
		g_warning("unknown paired device 0x%0x in slot %d (unreachable)",
			  hidpp_pid,
			  msg->device_id);
	}

	return TRUE;
}

static void
fu_logitech_hidpp_runtime_bolt_poll_peripherals(FuDevice *device)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimeBolt *bolt = FU_HIDPP_RUNTIME_BOLT(device);

	for (guint i = 1; i <= bolt->pairing_slots; i++) {
		g_autofree gchar *name = NULL;
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
		g_autoptr(GError) error_local = NULL;
		guint16 hidpp_pid;

		name = fu_logitech_hidpp_runtime_bolt_query_device_name(self, i, &error_local);
		if (name == NULL) {
			g_debug("cannot query paired device name for slot %u", i);
			continue;
		}

		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
		msg->sub_id = FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER;
		msg->function_id = BOLT_REGISTER_PAIRING_INFORMATION;
		msg->data[0] = 0x50 | i; /* pairing information */
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_transfer(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
						msg,
						&error_local))
			continue;
		hidpp_pid = (msg->data[2] << 8) | msg->data[3];
		if ((msg->data[1] & 0x40) == 0) {
			/* paired device is reachable */
			g_autoptr(FuLogitechHidppDevice) child = NULL;
			child = fu_logitech_hidpp_device_new(FU_UDEV_DEVICE(device));
			fu_device_set_install_duration(FU_DEVICE(child), 270);
			fu_device_add_private_flag(FU_DEVICE(child),
						   FU_LOGITECH_HIDPP_DEVICE_FLAG_ADD_RADIO);
			fu_device_set_name(FU_DEVICE(child), name);
			fu_logitech_hidpp_device_set_device_idx(child, i);
			fu_logitech_hidpp_device_set_hidpp_pid(child, hidpp_pid);
			if (!fu_device_probe(FU_DEVICE(child), &error_local))
				continue;
			if (!fu_device_setup(FU_DEVICE(child), &error_local))
				continue;
			fu_device_add_child(device, FU_DEVICE(child));
		}
	}
}

static gboolean
fu_logitech_hidpp_runtime_bolt_process_notification(FuLogitechHidppRuntimeBolt *self,
						    FuLogitechHidppHidppMsg *msg)
{
	g_autoptr(GError) error_local = NULL;

	/* HID++1.0 error */
	if (!fu_logitech_hidpp_msg_is_error(msg, &error_local)) {
		g_warning("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* unifying receiver notification */
	if (msg->report_id == FU_LOGITECH_HIDPP_REPORT_ID_SHORT) {
		switch (msg->sub_id) {
		case FU_LOGITECH_HIDPP_SUBID_DEVICE_CONNECTION:
		case FU_LOGITECH_HIDPP_SUBID_DEVICE_DISCONNECTION:
		case FU_LOGITECH_HIDPP_SUBID_DEVICE_LOCKING_CHANGED:
			if (!fu_logitech_hidpp_runtime_bolt_update_paired_device(self,
										 msg,
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
			g_debug("unknown SubID %02x", msg->sub_id);
			break;
		}
	}

	return TRUE;
}

static FuLogitechHidppHidppMsg *
fu_logitech_hidpp_runtime_bolt_find_newest_msg(GPtrArray *msgs, guint8 device_id, guint8 sub_id)
{
	for (guint i = 0; i < msgs->len; i++) {
		FuLogitechHidppHidppMsg *msg = g_ptr_array_index(msgs, msgs->len - (i + 1));
		if (msg->device_id == device_id && msg->sub_id == sub_id)
			return msg;
	}
	return NULL;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_poll(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *runtime = FU_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimeBolt *self = FU_HIDPP_RUNTIME_BOLT(device);
	const guint timeout = 1; /* ms */
	g_autoptr(GPtrArray) msgs = g_ptr_array_new_with_free_func(g_free);

	/* open -- not a locker as we have no kernel driver */
	if (!fu_device_open(device, error))
		return FALSE;

	/* drain all the pending messages into the array */
	while (TRUE) {
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
		g_autoptr(GError) error_local = NULL;
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_receive(
			fu_udev_device_get_io_channel(FU_UDEV_DEVICE(runtime)),
			msg,
			timeout,
			&error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT))
				break;
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "error polling Bolt receiver: ");
			return FALSE;
		}
		g_ptr_array_add(msgs, g_steal_pointer(&msg));
	}

	/* process messages in order, but discard any message with a newer version */
	for (guint i = 0; i < msgs->len; i++) {
		FuLogitechHidppHidppMsg *msg = g_ptr_array_index(msgs, i);
		FuLogitechHidppHidppMsg *msg_newest;

		/* find the newest message with the matching device and sub-IDs */
		msg_newest = fu_logitech_hidpp_runtime_bolt_find_newest_msg(msgs,
									    msg->device_id,
									    msg->sub_id);
		if (msg != msg_newest) {
			g_debug("ignoring duplicate message device-id:%02x [%s] sub-id:%02x [%s]",
				msg->device_id,
				fu_logitech_hidpp_device_idx_to_string(msg->device_id),
				msg->sub_id,
				fu_logitech_hidpp_subid_to_string(msg->sub_id));
			continue;
		}
		fu_logitech_hidpp_runtime_bolt_process_notification(self, msg);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_setup_internal(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidppRuntimeBolt *bolt = FU_HIDPP_RUNTIME_BOLT(device);
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();

	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
	msg->sub_id = FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER;
	msg->function_id = BOLT_REGISTER_PAIRING_INFORMATION;
	msg->data[0] = 0x02; /* FW Version (contains the number of pairing slots) */
	msg->hidpp_version = 1;
	if (!fu_logitech_hidpp_transfer(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
					msg,
					error)) {
		g_prefix_error(error, "failed to fetch the number of pairing slots: ");
		return FALSE;
	}
	bolt->pairing_slots = msg->data[8];

	/*
	 * TODO: Iterate only over the first three entity indexes for
	 * now.
	 */
	for (guint i = 0; i < 3; i++) {
		guint16 version_raw = 0;
		g_autofree gchar *version = NULL;
		g_autoptr(FuLogitechHidppRadio) radio = NULL;
		g_autoptr(GString) radio_version = NULL;

		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
		msg->sub_id = FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER;
		msg->function_id = BOLT_REGISTER_RECEIVER_FW_INFORMATION;
		msg->data[0] = i;
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_transfer(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
						msg,
						error)) {
			g_prefix_error(error, "failed to read device config: ");
			return FALSE;
		}

		switch (msg->data[0]) {
		case 0:
			/* main application */
			if (!fu_memread_uint16_safe(msg->data,
						    sizeof(msg->data),
						    0x03,
						    &version_raw,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			version = fu_logitech_hidpp_format_version("MPR",
								   msg->data[1],
								   msg->data[2],
								   version_raw);
			fu_device_set_version(device, version);
			break;
		case 1:
			/* bootloader */
			if (!fu_memread_uint16_safe(msg->data,
						    sizeof(msg->data),
						    0x03,
						    &version_raw,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			version = fu_logitech_hidpp_format_version("BOT",
								   msg->data[1],
								   msg->data[2],
								   version_raw);
			fu_device_set_version_bootloader(device, version);
			break;
		case 5:
			/* SoftDevice */
			radio_version = g_string_new(NULL);
			radio = fu_logitech_hidpp_radio_new(ctx, i);
			fu_device_add_instance_u16(FU_DEVICE(radio),
						   "VEN",
						   fu_device_get_vid(device));
			fu_device_add_instance_u16(FU_DEVICE(radio),
						   "DEV",
						   fu_device_get_pid(device));
			fu_device_add_instance_u8(FU_DEVICE(radio), "ENT", msg->data[0]);
			fu_device_incorporate(FU_DEVICE(radio),
					      FU_DEVICE(device),
					      FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
			fu_device_set_logical_id(FU_DEVICE(radio), "Receiver_SoftDevice");
			if (!fu_device_build_instance_id(FU_DEVICE(radio),
							 error,
							 "HIDRAW",
							 "VEN",
							 "DEV",
							 "ENT",
							 NULL))
				return FALSE;
			if (!fu_memread_uint16_safe(msg->data,
						    sizeof(msg->data),
						    0x03,
						    &version_raw,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			g_string_append_printf(radio_version, "0x%.4x", version_raw);
			fu_device_set_version(FU_DEVICE(radio), radio_version->str);
			fu_device_add_child(device, FU_DEVICE(radio));
			break;
		default:
			break;
		}
	}

	/* enable HID++ notifications */
	if (!fu_logitech_hidpp_runtime_enable_notifications(self, error)) {
		g_prefix_error(error, "failed to enable notifications: ");
		return FALSE;
	}
	fu_logitech_hidpp_runtime_bolt_poll_peripherals(device);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_setup(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	for (guint i = 0; i < 5; i++) {
		g_clear_error(&error_local);
		/* HID++1.0 devices have to sleep to allow Solaar to talk to
		 * the device first -- we can't use the SwID as this is a
		 * HID++2.0 feature */
		fu_device_sleep(device, 200); /* ms */
		if (fu_logitech_hidpp_runtime_bolt_setup_internal(device, &error_local))
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
fu_logitech_hidpp_runtime_bolt_class_init(FuLogitechHidppRuntimeBoltClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_logitech_hidpp_runtime_bolt_detach;
	device_class->setup = fu_logitech_hidpp_runtime_bolt_setup;
	device_class->poll = fu_logitech_hidpp_runtime_bolt_poll;
	device_class->to_string = fu_logitech_hidpp_runtime_bolt_to_string;
}

static void
fu_logitech_hidpp_runtime_bolt_init(FuLogitechHidppRuntimeBolt *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_vendor(FU_DEVICE(self), "Logitech");
	fu_device_set_name(FU_DEVICE(self), "Bolt Receiver");
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
}
