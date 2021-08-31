/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-runtime-bolt.h"

struct _FuLogitechHidPpRuntimeBolt {
	FuLogitechHidPpRuntime parent_instance;
	guint8 pairing_slots;
};

G_DEFINE_TYPE(FuLogitechHidPpRuntimeBolt, fu_logitech_hidpp_runtime_bolt, FU_TYPE_HIDPP_RUNTIME)

static gboolean
fu_logitech_hidpp_runtime_bolt_detach(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME(device);
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	msg->report_id = HIDPP_REPORT_ID_LONG;
	msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
	msg->sub_id = HIDPP_SUBID_SET_LONG_REGISTER;
	msg->function_id = BOLT_REGISTER_DFU_CONTROL;
	msg->data[0] = 1; /* Enable DFU */
	msg->data[4] = 'P';
	msg->data[5] = 'R';
	msg->data[6] = 'E';
	msg->hidpp_version = 1;
	msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_send(fu_logitech_hidpp_runtime_get_io_channel(self),
				    msg,
				    FU_UNIFYING_DEVICE_TIMEOUT_MS,
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
	/* generate a message if not already set */
	if (fu_device_get_update_message(device) == NULL) {
		g_autofree gchar *str = NULL;
		str = g_strdup_printf("%s needs to be manually restarted to complete the update. "
				      "Please unplug it and plug it back again.",
				      fu_device_get_name(device));
		fu_device_set_update_message(device, str);
	}
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_set_message(request, fu_device_get_update_message(device));
	fu_device_emit_request(device, request);
	return TRUE;
}

static void
fu_logitech_hidpp_runtime_bolt_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidPpRuntimeBolt *self = FU_HIDPP_RUNTIME_BOLT(device);

	FU_DEVICE_CLASS(fu_logitech_hidpp_runtime_bolt_parent_class)->to_string(device, idt, str);
	fu_common_string_append_ku(str, idt, "PairingSlots", self->pairing_slots);
}

static gboolean
fu_logitech_hidpp_runtime_bolt_process_notification(FuLogitechHidPpRuntimeBolt *self,
						    FuLogitechHidPpHidppMsg *msg)
{
	g_autoptr(GError) error_local = NULL;

	/* HID++1.0 error */
	if (!fu_logitech_hidpp_msg_is_error(msg, &error_local)) {
		g_warning("failed to get pending read: %s", error_local->message);
		return TRUE;
	}

	/* unifying receiver notification */
	if (msg->report_id == HIDPP_REPORT_ID_SHORT) {
		switch (msg->sub_id) {
		case HIDPP_SUBID_DEVICE_CONNECTION:
		case HIDPP_SUBID_DEVICE_DISCONNECTION:
		case HIDPP_SUBID_DEVICE_LOCKING_CHANGED:
			g_debug("ignoring device message");
			break;
		case HIDPP_SUBID_LINK_QUALITY:
			g_debug("ignoring link quality message");
			break;
		case HIDPP_SUBID_ERROR_MSG:
			g_debug("ignoring link quality message");
			break;
		default:
			g_debug("unknown SubID %02x", msg->sub_id);
			break;
		}
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_poll(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *runtime = FU_HIDPP_RUNTIME(device);
	FuLogitechHidPpRuntimeBolt *self = FU_HIDPP_RUNTIME_BOLT(device);
	const guint timeout = 1; /* ms */
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();

	if (!fu_device_open(device, error))
		return FALSE;

	/* is there any pending data to read */
	msg->hidpp_version = 1;
	while (fu_logitech_hidpp_receive(fu_logitech_hidpp_runtime_get_io_channel(runtime),
					 msg,
					 timeout,
					 &error_local)) {
		fu_logitech_hidpp_runtime_bolt_process_notification(self, msg);
	}
	if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		g_prefix_error(error, "Error polling Bolt receiver: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_bolt_setup_internal(FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME(device);
	FuLogitechHidPpRuntimeBolt *bolt = FU_HIDPP_RUNTIME_BOLT(device);
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new();

	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
	msg->sub_id = HIDPP_SUBID_GET_LONG_REGISTER;
	msg->function_id = BOLT_REGISTER_PAIRING_INFORMATION;
	msg->data[0] = 0x02; /* FW Version (contains the number of pairing slots) */
	msg->hidpp_version = 1;
	if (!fu_logitech_hidpp_transfer(fu_logitech_hidpp_runtime_get_io_channel(self),
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
		g_autofree gchar *version = NULL;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
		msg->sub_id = HIDPP_SUBID_GET_LONG_REGISTER;
		msg->function_id = BOLT_REGISTER_RECEIVER_FW_INFORMATION;
		msg->data[0] = i;
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_transfer(fu_logitech_hidpp_runtime_get_io_channel(self),
						msg,
						error)) {
			g_prefix_error(error, "failed to read device config: ");
			return FALSE;
		}
		switch (msg->data[0]) {
		case 0:
			/* main application */
			version = fu_logitech_hidpp_format_version("MPR",
								   msg->data[1],
								   msg->data[2],
								   (guint16)msg->data[3] << 8 |
								       msg->data[4]);
			fu_device_set_version(device, version);
			break;
		case 1:
			/* bootloader */
			version = fu_logitech_hidpp_format_version("BOT",
								   msg->data[1],
								   msg->data[2],
								   (guint16)msg->data[3] << 8 |
								       msg->data[4]);
			fu_device_set_version_bootloader(device, version);
			break;
		case 5:
			/* SoftDevice */
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
		g_usleep(200 * 1000);
		if (fu_logitech_hidpp_runtime_bolt_setup_internal(device, &error_local))
			return TRUE;
		if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_DATA)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	g_propagate_error(error, g_steal_pointer(&error_local));
	return FALSE;
}

static void
fu_logitech_hidpp_runtime_bolt_class_init(FuLogitechHidPpRuntimeBoltClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->detach = fu_logitech_hidpp_runtime_bolt_detach;
	klass_device->setup = fu_logitech_hidpp_runtime_bolt_setup;
	klass_device->poll = fu_logitech_hidpp_runtime_bolt_poll;
	klass_device->to_string = fu_logitech_hidpp_runtime_bolt_to_string;
}

static void
fu_logitech_hidpp_runtime_bolt_init(FuLogitechHidPpRuntimeBolt *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_name(FU_DEVICE(self), "Bolt Receiver");
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
}
