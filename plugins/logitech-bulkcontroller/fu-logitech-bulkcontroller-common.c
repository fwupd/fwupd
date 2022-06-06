/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-logitech-bulkcontroller-common.h"
#include "usb_msg.pb-c.h"

static void
proto_manager_set_header(Logi__Device__Proto__Header *header_msg)
{
	gint64 timestamp_tv;

	g_return_if_fail(header_msg != NULL);

	timestamp_tv = g_get_real_time();
	header_msg->id = g_uuid_string_random();
	header_msg->timestamp = g_strdup_printf("%" G_GINT64_FORMAT, timestamp_tv / 1000);
}

GByteArray *
proto_manager_generate_get_device_info_request(void)
{
	GByteArray *buf = g_byte_array_new();
	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__GetDeviceInfoRequest get_deviceinfo_msg =
	    LOGI__DEVICE__PROTO__GET_DEVICE_INFO_REQUEST__INIT;
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	Logi__Device__Proto__Request request_msg = LOGI__DEVICE__PROTO__REQUEST__INIT;
	request_msg.payload_case = LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_GET_DEVICE_INFO_REQUEST;
	request_msg.get_device_info_request = &get_deviceinfo_msg;

	proto_manager_set_header(&header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	fu_byte_array_set_size(buf, logi__device__proto__usb_msg__get_packed_size(&usb_msg), 0x00);
	logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)buf->data);
	return buf;
}

GByteArray *
proto_manager_generate_transition_to_device_mode_request(void)
{
	GByteArray *buf = g_byte_array_new();
	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__TransitionToDeviceModeRequest transition_to_device_mode_msg =
	    LOGI__DEVICE__PROTO__TRANSITION_TO_DEVICE_MODE_REQUEST__INIT;
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	Logi__Device__Proto__Request request_msg = LOGI__DEVICE__PROTO__REQUEST__INIT;
	request_msg.payload_case =
	    LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_TRANSITION_TO_DEVICEMODE_REQUEST;
	request_msg.transition_to_devicemode_request = &transition_to_device_mode_msg;

	proto_manager_set_header(&header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	fu_byte_array_set_size(buf, logi__device__proto__usb_msg__get_packed_size(&usb_msg), 0x00);
	logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)buf->data);
	return buf;
}

GByteArray *
proto_manager_generate_set_device_time_request(void)
{
	GByteArray *buf = g_byte_array_new();
#if GLIB_CHECK_VERSION(2, 57, 1)
	g_autoptr(GTimeZone) tz = g_time_zone_new_local();
#else
	g_autoptr(GDateTime) dt = g_date_time_new_now_utc();
#endif

	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__SetDeviceTimeRequest set_devicetime_msg =
	    LOGI__DEVICE__PROTO__SET_DEVICE_TIME_REQUEST__INIT;
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	Logi__Device__Proto__Request request_msg = LOGI__DEVICE__PROTO__REQUEST__INIT;
	request_msg.payload_case = LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_SET_DEVICE_TIME_REQUEST;
	request_msg.set_device_time_request = &set_devicetime_msg;

#if GLIB_CHECK_VERSION(2, 57, 1)
	set_devicetime_msg.ts = (g_get_real_time() / 1000) + SET_TIME_DELAY_MS;
	set_devicetime_msg.time_zone = g_strdup_printf("%s", g_time_zone_get_identifier(tz));
#else
	set_devicetime_msg.ts = (g_date_time_to_unix(dt) * 1000) + SET_TIME_DELAY_MS;
	set_devicetime_msg.time_zone = g_strdup_printf("%s", "UTC");
#endif
	proto_manager_set_header(&header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	fu_byte_array_set_size(buf, logi__device__proto__usb_msg__get_packed_size(&usb_msg), 0x00);
	logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)buf->data);
	return buf;
}

GByteArray *
proto_manager_decode_message(const guint8 *data,
			     guint32 len,
			     FuLogitechBulkcontrollerProtoId *proto_id,
			     GError **error)
{
	g_autoptr(GByteArray) buf_decoded = g_byte_array_new();
	guint32 success = 0;
	guint32 error_code = 0;
	Logi__Device__Proto__UsbMsg *usb_msg =
	    logi__device__proto__usb_msg__unpack(NULL, len, (const unsigned char *)data);
	if (usb_msg == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "unable to unpack data");
		return NULL;
	}

	switch (usb_msg->message_case) {
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_ACK:
		*proto_id = kProtoId_Ack;
		break;
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_RESPONSE:
		if (!usb_msg->response) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "no USB response");
			return NULL;
		}
		switch (usb_msg->response->payload_case) {
		case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_GET_DEVICE_INFO_RESPONSE:
			if (usb_msg->response->get_device_info_response) {
				const gchar *tmp =
				    usb_msg->response->get_device_info_response->payload;
				*proto_id = kProtoId_GetDeviceInfoResponse;
				if (tmp != NULL)
					g_byte_array_append(buf_decoded,
							    (const guint8 *)tmp,
							    strlen(tmp));
			}
			break;
		case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_TRANSITION_TO_DEVICEMODE_RESPONSE:
			if (usb_msg->response->transition_to_devicemode_response) {
				*proto_id = kProtoId_TransitionToDeviceModeResponse;
				success =
				    usb_msg->response->transition_to_devicemode_response->success
					? 1
					: 0;
				error_code =
				    usb_msg->response->transition_to_devicemode_response->error;
				fu_byte_array_append_uint32(buf_decoded, success, G_LITTLE_ENDIAN);
				fu_byte_array_append_uint32(buf_decoded,
							    error_code,
							    G_LITTLE_ENDIAN);
			}
			break;
		default:
			break;
		};
		break;
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_EVENT:
		if (!usb_msg->response) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "no USB event");
			return NULL;
		}
		switch (usb_msg->event->payload_case) {
		case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_KONG_EVENT:
			if (usb_msg->event->kong_event) {
				const gchar *tmp = usb_msg->event->kong_event->mqtt_event;
				*proto_id = kProtoId_KongEvent;
				if (tmp != NULL)
					g_byte_array_append(buf_decoded,
							    (const guint8 *)tmp,
							    strlen(tmp));
			}
			break;
		case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_HANDSHAKE_EVENT:
			if (usb_msg->event->handshake_event) {
				*proto_id = kProtoId_HandshakeEvent;
			}
			break;
		case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_CRASH_DUMP_AVAILABLE_EVENT:
			*proto_id = kProtoId_CrashDumpAvailableEvent;
			break;
		default:
			break;
		};
		break;
	default:
		break;
	};
	logi__device__proto__usb_msg__free_unpacked(usb_msg, NULL);
	return g_steal_pointer(&buf_decoded);
}

const gchar *
fu_logitech_bulkcontroller_device_status_to_string(FuLogitechBulkcontrollerDeviceStatus status)
{
	if (status == kDeviceStateUnknown)
		return "Unknown";
	if (status == kDeviceStateOffline)
		return "Offline";
	if (status == kDeviceStateOnline)
		return "Online";
	if (status == kDeviceStateIdle)
		return "Idle";
	if (status == kDeviceStateInUse)
		return "InUse";
	if (status == kDeviceStateAudioOnly)
		return "AudioOnly";
	if (status == kDeviceStateEnumerating)
		return "Enumerating";
	return NULL;
}

const gchar *
fu_logitech_bulkcontroller_device_update_state_to_string(
    FuLogitechBulkcontrollerDeviceUpdateState update_state)
{
	if (update_state == kUpdateStateUnknown)
		return "Unknown";
	if (update_state == kUpdateStateCurrent)
		return "Current";
	if (update_state == kUpdateStateAvailable)
		return "Available";
	if (update_state == kUpdateStateStarting)
		return "Starting";
	if (update_state == kUpdateStateDownloading)
		return "Downloading";
	if (update_state == kUpdateStateReady)
		return "Ready";
	if (update_state == kUpdateStateUpdating)
		return "Updating";
	if (update_state == kUpdateStateScheduled)
		return "Scheduled";
	if (update_state == kUpdateStateError)
		return "Error";
	return NULL;
}
