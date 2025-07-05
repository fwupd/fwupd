/*
 * Copyright 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-logitech-bulkcontroller-common.h"
#include "usb_msg.pb-c.h"

static void
fu_logitech_bulkcontroller_proto_manager_set_header(FuDevice *device,
						    Logi__Device__Proto__Header *header_msg)
{
	gint64 timestamp_tv;

	g_return_if_fail(header_msg != NULL);

	/* make predictable */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG)) {
		header_msg->id = 0;
		header_msg->timestamp = g_strdup("0");
		return;
	}

	timestamp_tv = g_get_real_time();
	header_msg->id = g_uuid_string_random();
	header_msg->timestamp = g_strdup_printf("%" G_GINT64_FORMAT, timestamp_tv / 1000);
}

GByteArray *
fu_logitech_bulkcontroller_proto_manager_generate_get_device_info_request(FuDevice *device)
{
	GByteArray *buf = g_byte_array_new();
	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__GetDeviceInfoRequest get_deviceinfo_msg =
	    LOGI__DEVICE__PROTO__GET_DEVICE_INFO_REQUEST__INIT;
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	Logi__Device__Proto__Request request_msg = LOGI__DEVICE__PROTO__REQUEST__INIT;
	request_msg.payload_case = LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_GET_DEVICE_INFO_REQUEST;
	request_msg.get_device_info_request = &get_deviceinfo_msg;

	fu_logitech_bulkcontroller_proto_manager_set_header(device, &header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	fu_byte_array_set_size(buf, logi__device__proto__usb_msg__get_packed_size(&usb_msg), 0x00);
	logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)buf->data);
	g_free(header_msg.id);
	g_free(header_msg.timestamp);
	return buf;
}

GByteArray *
fu_logitech_bulkcontroller_proto_manager_generate_transition_to_device_mode_request(
    FuDevice *device)
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

	fu_logitech_bulkcontroller_proto_manager_set_header(device, &header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	fu_byte_array_set_size(buf, logi__device__proto__usb_msg__get_packed_size(&usb_msg), 0x00);
	logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)buf->data);
	g_free(header_msg.id);
	g_free(header_msg.timestamp);
	return buf;
}

GByteArray *
fu_logitech_bulkcontroller_proto_manager_generate_set_device_time_request(FuDevice *device,
									  GError **error)
{
	g_autofree gchar *olson_location = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__SetDeviceTimeRequest set_devicetime_msg =
	    LOGI__DEVICE__PROTO__SET_DEVICE_TIME_REQUEST__INIT;
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	Logi__Device__Proto__Request request_msg = LOGI__DEVICE__PROTO__REQUEST__INIT;

	/* the device expects an olson_location, not a timezone */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG)) {
		olson_location = g_strdup("Europe/London");
	} else {
		olson_location = fu_common_get_olson_timezone_id(error);
		if (olson_location == NULL)
			return NULL;
	}

	request_msg.payload_case = LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_SET_DEVICE_TIME_REQUEST;
	request_msg.set_device_time_request = &set_devicetime_msg;

	/* make predictable */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG)) {
		set_devicetime_msg.ts = 0;
	} else {
		set_devicetime_msg.ts = (g_get_real_time() / 1000) + SET_TIME_DELAY_MS;
	}
	set_devicetime_msg.time_zone = olson_location;
	fu_logitech_bulkcontroller_proto_manager_set_header(device, &header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	fu_byte_array_set_size(buf, logi__device__proto__usb_msg__get_packed_size(&usb_msg), 0x00);
	logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)buf->data);
	g_free(header_msg.id);
	g_free(header_msg.timestamp);
	return g_steal_pointer(&buf);
}

static GByteArray *
fu_logitech_bulkcontroller_usb_msg_parse_msg_response(Logi__Device__Proto__UsbMsg *usb_msg,
						      FuLogitechBulkcontrollerProtoId *proto_id,
						      GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (usb_msg->response == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no USB response");
		return NULL;
	}
	switch (usb_msg->response->payload_case) {
	case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_GET_DEVICE_INFO_RESPONSE:
		if (usb_msg->response->get_device_info_response) {
			const gchar *tmp = usb_msg->response->get_device_info_response->payload;
			*proto_id = kProtoId_GetDeviceInfoResponse;
			if (tmp != NULL)
				g_byte_array_append(buf, (const guint8 *)tmp, strlen(tmp));
		}
		break;
	case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_TRANSITION_TO_DEVICEMODE_RESPONSE:
		if (usb_msg->response->transition_to_devicemode_response) {
			*proto_id = kProtoId_TransitionToDeviceModeResponse;
			if (!usb_msg->response->transition_to_devicemode_response->success) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "transition mode request failed. error: %u",
					    (guint)usb_msg->response
						->transition_to_devicemode_response->error);
				return NULL;
			}
		}
		break;
	default:
		break;
	};

	/* success */
	return g_steal_pointer(&buf);
}

static GByteArray *
fu_logitech_bulkcontroller_usb_msg_parse_msg_event(Logi__Device__Proto__UsbMsg *usb_msg,
						   FuLogitechBulkcontrollerProtoId *proto_id,
						   GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (usb_msg->response == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no USB event");
		return NULL;
	}
	switch (usb_msg->event->payload_case) {
	case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_KONG_EVENT:
		if (usb_msg->event->kong_event) {
			const gchar *tmp = usb_msg->event->kong_event->mqtt_event;
			*proto_id = kProtoId_KongEvent;
			if (tmp != NULL)
				g_byte_array_append(buf, (const guint8 *)tmp, strlen(tmp));
		}
		break;
	case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_HANDSHAKE_EVENT:
		if (usb_msg->event->handshake_event)
			*proto_id = kProtoId_HandshakeEvent;
		break;
	case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_CRASH_DUMP_AVAILABLE_EVENT:
		*proto_id = kProtoId_CrashDumpAvailableEvent;
		break;
	default:
		break;
	};

	/* success */
	return g_steal_pointer(&buf);
}

GByteArray *
fu_logitech_bulkcontroller_proto_manager_decode_message(const guint8 *data,
							guint32 len,
							FuLogitechBulkcontrollerProtoId *proto_id,
							GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	Logi__Device__Proto__UsbMsg *usb_msg =
	    logi__device__proto__usb_msg__unpack(NULL, len, (const unsigned char *)data);

	/* sanity check */
	if (usb_msg == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unable to unpack data");
		return NULL;
	}

	switch (usb_msg->message_case) {
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_ACK:
		buf = g_byte_array_new();
		*proto_id = kProtoId_Ack;
		break;
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_RESPONSE:
		buf =
		    fu_logitech_bulkcontroller_usb_msg_parse_msg_response(usb_msg, proto_id, error);
		if (buf == NULL)
			return NULL;
		break;
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_EVENT:
		buf = fu_logitech_bulkcontroller_usb_msg_parse_msg_event(usb_msg, proto_id, error);
		if (buf == NULL)
			return NULL;
		break;
	default:
		buf = g_byte_array_new();
		g_debug("ignoring invalid message case 0x%x", usb_msg->message_case);
		break;
	};
	logi__device__proto__usb_msg__free_unpacked(usb_msg, NULL);
	return g_steal_pointer(&buf);
}
