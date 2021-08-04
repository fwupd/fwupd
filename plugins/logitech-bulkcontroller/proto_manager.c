/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "proto_manager.h"

#include <json-glib/json-glib.h>

#include "usb_msg.pb-c.h"

/*!
 * @brief Set header values
 * @param header_msg pointer to Logi__Device__Proto__Header
 */
void
proto_manager_set_header(Logi__Device__Proto__Header *header_msg)
{
	gint64 timestamp_tv;

	if (!header_msg)
		return;

	timestamp_tv = g_get_real_time();
	header_msg->id = g_uuid_string_random();
	header_msg->timestamp = g_strdup_printf("%" G_GINT64_FORMAT, timestamp_tv / 1000);
}

/*!
 * @brief Generate a GetDeviceInfo request
 * @param message - struct that will contain the message data. The caller have to free the data
 * after use.
 * @return 0 if success, otherwise the error code
 */
int
proto_manager_generate_get_device_info_request(Message *message)
{
	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__GetDeviceInfoRequest get_deviceinfo_msg =
	    LOGI__DEVICE__PROTO__GET_DEVICE_INFO_REQUEST__INIT;
	Logi__Device__Proto__Request request_msg = {
	    PROTOBUF_C_MESSAGE_INIT(&logi__device__proto__request__descriptor),
	    LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_GET_DEVICE_INFO_REQUEST,
	    {&get_deviceinfo_msg}};
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	size_t packed_len;

	if (!message)
		return 1;

	proto_manager_set_header(&header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	message->len = logi__device__proto__usb_msg__get_packed_size(&usb_msg);
	message->data = g_malloc(message->len);

	packed_len = logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)message->data);
	return packed_len == message->len ? 0 : 1;
}

/*!
 * @brief Generate a TransitionToDeviceMode request
 * @param message - struct that will contain the message data.  The caller have to free the data
 * after use.
 * @return 0 if success, otherwise the error code
 */
int
proto_manager_generate_transition_to_device_mode_request(Message *message)
{
	Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
	Logi__Device__Proto__TransitionToDeviceModeRequest transition_to_device_mode_msg =
	    LOGI__DEVICE__PROTO__TRANSITION_TO_DEVICE_MODE_REQUEST__INIT;
	Logi__Device__Proto__Request request_msg = {
	    PROTOBUF_C_MESSAGE_INIT(&logi__device__proto__request__descriptor),
	    LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_TRANSITION_TO_DEVICEMODE_REQUEST,
	    {&transition_to_device_mode_msg}};
	Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
	size_t packed_len;

	if (!message)
		return 1;

	proto_manager_set_header(&header_msg);
	usb_msg.header = &header_msg;
	usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
	usb_msg.request = &request_msg;

	message->len = logi__device__proto__usb_msg__get_packed_size(&usb_msg);
	message->data = g_malloc(message->len);

	packed_len = logi__device__proto__usb_msg__pack(&usb_msg, (unsigned char *)message->data);
	return packed_len == message->len ? 0 : 1;
}

/*!
 * @brief Decode the incoming message
 * @param data - contains the incoming message to be parsed
 * @param len - size of the array pointer message
 * @param decoded_data - decoded data
 * @return the proto id
 */
Proto_id
proto_manager_decode_message(const gchar *data, guint32 len, DecodedData *decoded_data)
{
	Proto_id proto_id = kProtoId_UnknownId;

	Logi__Device__Proto__UsbMsg *usb_msg =
	    logi__device__proto__usb_msg__unpack(NULL, len, (const unsigned char *)data);

	if (usb_msg == NULL) {
		g_debug("Unable to unpack data");
		return proto_id;
	}

	switch (usb_msg->message_case) {
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_ACK:
		g_debug(
		    "[proto_manager_decode_message] - LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_ACK");
		proto_id = kProtoId_Ack;
		break;
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_RESPONSE: {
		if (!usb_msg->response)
			return proto_id;
		g_debug("[proto_manager_decode_message] - "
			"LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_RESPONSE");
		switch (usb_msg->response->payload_case) {
		case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_GET_DEVICE_INFO_RESPONSE:
			g_debug("[proto_manager_decode_message] - "
				"LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_GET_DEVICE_INFO_RESPONSE");
			if (usb_msg->response->get_device_info_response) {
				proto_id = kProtoId_GetDeviceInfoResponse;
				if (usb_msg->response->get_device_info_response->payload) {
					g_stpcpy(
					    (gchar *)&decoded_data->device_info,
					    usb_msg->response->get_device_info_response->payload);
				}
			}
			break;
		case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_TRANSITION_TO_DEVICEMODE_RESPONSE:
			g_debug("[proto_manager_decode_message] - "
				"LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_TRANSITION_TO_DEVICEMODE_"
				"RESPONSE");
			if (usb_msg->response->transition_to_devicemode_response) {
				proto_id = kProtoId_TransitionToDeviceModeResponse;
				decoded_data->transition_to_device_mode =
				    usb_msg->response->transition_to_devicemode_response->success;
			}
			break;
		default:
			g_debug("[proto_manager_decode_message] - Unhandled response %u",
				usb_msg->response->payload_case);
			break;
		};
	} break;
	case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_EVENT: {
		if (!usb_msg->event)
			return proto_id;
		g_debug(
		    "[proto_manager_decode_message] - LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_EVENT");
		switch (usb_msg->event->payload_case) {
		case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_KONG_EVENT: {
			g_debug("[proto_manager_decode_message] - "
				"LOGI__DEVICE__PROTO__EVENT__PAYLOAD_KONG_EVENT");
			if (usb_msg->event->kong_event) {
				proto_id = kProtoId_KongEvent;
				if (usb_msg->event->kong_event->mqtt_event) {
					g_stpcpy((gchar *)&decoded_data->device_info,
						 usb_msg->event->kong_event->mqtt_event);
				}
			}

		} break;
		case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_HANDSHAKE_EVENT: {
			g_debug("[proto_manager_decode_message] - "
				"LOGI__DEVICE__PROTO__EVENT__PAYLOAD_HANDSHAKE_EVENT");
			if (usb_msg->event->handshake_event) {
				proto_id = kProtoId_HandshakeEvent;
				decoded_data->hand_shake_event = TRUE;
			}
		} break;
		case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_CRASH_DUMP_AVAILABLE_EVENT: {
			g_debug("[proto_manager_decode_message] - "
				"LOGI__DEVICE__PROTO__EVENT__PAYLOAD_CRASH_DUMP_AVAILABLE_EVENT");
			proto_id = kProtoId_CrashDumpAvailableEvent;
		} break;
		default:
			g_debug("[proto_manager_decode_message] - Unhandled event %u\n",
				usb_msg->event->payload_case);
			break;
		};
	} break;
	default:
		g_debug("[proto_manager_decode_message] - Unknown");
		break;
	};
	logi__device__proto__usb_msg__free_unpacked(usb_msg, NULL);
	return proto_id;
}

/*!
 * @brief Parse the device info.  Uses
 * https://gitlab.gnome.org/GNOME/json-glib/
 * https://wiki.gnome.org/Projects/JsonGlib
 * @param data - string containing the device info
 * @param dev_info - contains the device information
 * @return true if parsing is successful
 */
gboolean
proto_manager_parse_device_info(const gchar *data, DeviceInfo *dev_info)
{
	JsonObject *device;
	JsonArray *devices;
	JsonNode *json_root;
	JsonObject *json_object;
	JsonObject *json_payload;
	gboolean result = FALSE;
	GError *err = NULL;
	g_autoptr(JsonParser) json_parser = NULL;
	/* Validate input */
	if (!data || !dev_info)
		return result;
	/* parse JSON reply */
	json_parser = json_parser_new();
	if (!json_parser_load_from_data(json_parser, data, -1, &err)) {
		g_warning("[proto_manager_parse_device_info] - Error in parsing json data %s",
			  err->message);
		g_error_free(err);
		return result;
	}

	g_debug("proto_manager_parse_device_info %s", data);

	json_root = json_parser_get_root(json_parser);
	if (json_root) {
		json_object = json_node_get_object(json_root);
		/* get any optional server message */
		json_payload = json_object_get_object_member(json_object, "payload");
		if (json_payload) {
			devices = json_object_get_array_member(json_payload, "devices");
			if (devices) {
				device = json_array_get_object_element(devices, 0);
				if (device) {
					if (json_object_has_member(device, "type"))
						g_stpcpy(
						    dev_info->type,
						    json_object_get_string_member(device, "type"));
					dev_info->device_type =
					    util_get_device_type(dev_info->type);
					if (json_object_has_member(device, "status"))
						dev_info->status = (DeviceState)
						    json_object_get_int_member(device, "status");
					else
						dev_info->status = -1;
					if (json_object_has_member(device, "updateStatus"))
						dev_info->update_status =
						    (UpdateState)json_object_get_int_member(
							device,
							"updateStatus");
					else
						dev_info->update_status = -1;
					if (json_object_has_member(device, "name"))
						g_stpcpy(
						    dev_info->name,
						    json_object_get_string_member(device, "name"));
					if (json_object_has_member(device, "sw"))
						g_stpcpy(
						    dev_info->sw,
						    json_object_get_string_member(device, "sw"));
					if (json_object_has_member(device, "manifest"))
						g_stpcpy(dev_info->manifest,
							 json_object_get_string_member(device,
										       "manifest"));
					if (json_object_has_member(device, "os"))
						g_stpcpy(
						    dev_info->os,
						    json_object_get_string_member(device, "os"));
					if (json_object_has_member(device, "osv"))
						g_stpcpy(
						    dev_info->osv,
						    json_object_get_string_member(device, "osv"));
					if (json_object_has_member(device, "serial"))
						g_stpcpy(dev_info->serial,
							 json_object_get_string_member(device,
										       "serial"));
					if (json_object_has_member(device, "buildType"))
						g_stpcpy(
						    dev_info->build_type,
						    json_object_get_string_member(device,
										  "buildType"));
					if (json_object_has_member(device, "hw"))
						g_stpcpy(
						    dev_info->hw,
						    json_object_get_string_member(device, "hw"));
					if (json_object_has_member(device, "ptv"))
						g_stpcpy(
						    dev_info->pan_tilt_version,
						    json_object_get_string_member(device, "ptv"));
					if (json_object_has_member(device, "pthw"))
						g_stpcpy(
						    dev_info->pan_tilt_hw,
						    json_object_get_string_member(device, "pthw"));
					if (json_object_has_member(device, "zfv"))
						g_stpcpy(
						    dev_info->zoom_focus_version,
						    json_object_get_string_member(device, "zfv"));
					if (json_object_has_member(device, "zfhw"))
						g_stpcpy(
						    dev_info->zoom_focus_hw,
						    json_object_get_string_member(device, "zfhw"));
					if (json_object_has_member(device, "hkv"))
						g_stpcpy(
						    dev_info->house_keeping_version,
						    json_object_get_string_member(device, "hkv"));
					if (json_object_has_member(device, "hkhw"))
						g_stpcpy(
						    dev_info->house_keeping_hw,
						    json_object_get_string_member(device, "hkhw"));
					if (json_object_has_member(device, "av"))
						g_stpcpy(
						    dev_info->audio_version,
						    json_object_get_string_member(device, "av"));
					if (json_object_has_member(device, "ahw"))
						g_stpcpy(
						    dev_info->audio_hw,
						    json_object_get_string_member(device, "ahw"));
					if (json_object_has_member(device, "updateProgress"))
						dev_info->update_progress =
						    json_object_get_int_member(device,
									       "updateProgress");
					else
						dev_info->update_progress = -1;
					if (json_object_has_member(device, "updateErrorCode"))
						g_stpcpy(dev_info->update_error_code,
							 json_object_get_string_member(
							     device,
							     "updateErrorCode"));
					result = TRUE;
				}
			}
		}
	}
	return result;
}

/*!
 * @brief Get the device type from the string type
 * @param type - string containing the device type
 * @return The DeviceType
 */
DeviceType
util_get_device_type(const gchar *type)
{
	gchar s_temp[25];
	DeviceType device_type = kDeviceTypeUnknown;
	if (!type)
		return device_type;
	stpcpy(s_temp, g_utf8_strdown(type, sizeof(s_temp)));
	if (g_strrstr(s_temp, "diddy") != NULL)
		return kDeviceTypeRallyBarMini;
	else if (g_strrstr(s_temp, "kong") != NULL)
		return kDeviceTypeRallyBar;
	return device_type;
}

void
util_print_device_info(DeviceInfo *dev_info)
{
	if (!dev_info)
		return;
	g_debug("[util_print_device_info] - type: %s ", dev_info->type);
	g_debug("[util_print_device_info] - Device type: %u ", dev_info->device_type);
	g_debug("[util_print_device_info] - name: %s ", dev_info->name);
	g_debug("[util_print_device_info] - Status: %d ", dev_info->status);
	g_debug("[util_print_device_info] - Update Status: %d ", dev_info->update_status);
	g_debug("[util_print_device_info] - sw: %s ", dev_info->sw);
	g_debug("[util_print_device_info] - manifest: %s ", dev_info->manifest);
	g_debug("[util_print_device_info] - osv: %s ", dev_info->osv);
	g_debug("[util_print_device_info] - serial: %s ", dev_info->serial);
	g_debug("[util_print_device_info] - build type: %s ", dev_info->build_type);
	g_debug("[util_print_device_info] - hw: %s ", dev_info->hw);
	g_debug("[util_print_device_info] - ptv: %s ", dev_info->pan_tilt_version);

	g_debug("[util_print_device_info] - pthw: %s ", dev_info->pan_tilt_hw);
	g_debug("[util_print_device_info] - hkv: %s ", dev_info->house_keeping_version);
	g_debug("[util_print_device_info] - hkhw: %s ", dev_info->house_keeping_hw);
	g_debug("[util_print_device_info] - av: %s ", dev_info->audio_version);
	g_debug("[util_print_device_info] - ahw: %s ", dev_info->audio_hw);
	if (dev_info->device_type == kDeviceTypeRallyBar) {
		g_debug("[util_print_device_info] - zfv: %s ", dev_info->zoom_focus_version);
		g_debug("[util_print_device_info] - zfhw: %s ", dev_info->zoom_focus_hw);
	}
	g_debug("[util_print_device_info] - Update Progress: %d ", dev_info->update_progress);
	g_debug("[util_print_device_info] - Update error code: %s ", dev_info->update_error_code);
}
