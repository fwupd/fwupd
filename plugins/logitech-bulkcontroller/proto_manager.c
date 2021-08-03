/*
 * proto_manager.c
 *
 *  Created on: Jul 29, 2021
 *      Author: edwin
 */

#include "proto_manager.h"
#include "usb_msg.pb-c.h"

/*!
* @brief Set header values
* @param header_msg pointer to Logi__Device__Proto__Header
*/
void proto_manager_set_header(Logi__Device__Proto__Header* header_msg) {
  if (!header_msg)
    return;

  gint64 timestamp_tv;
  timestamp_tv = g_get_real_time ();

  header_msg->id = g_uuid_string_random();
  header_msg->timestamp = g_strdup_printf ("%" G_GINT64_FORMAT, timestamp_tv / 1000);
}

/*!
 * @brief Generate a GetDeviceInfo request
 * @param message - struct that will contain the message data. The caller have to free the data after use.
 * @return 0 if success, otherwise the error code
 */
int proto_manager_generate_get_device_info_request(Message* message) {
  if (!message)
    return 1;
 Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
 Logi__Device__Proto__GetDeviceInfoRequest get_deviceinfo_msg = LOGI__DEVICE__PROTO__GET_DEVICE_INFO_REQUEST__INIT;
  
  proto_manager_set_header(&header_msg);
  
  Logi__Device__Proto__Request request_msg = {PROTOBUF_C_MESSAGE_INIT(&logi__device__proto__request__descriptor), LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_GET_DEVICE_INFO_REQUEST, {&get_deviceinfo_msg}};
  
  Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
  usb_msg.header = &header_msg;
  usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
  usb_msg.request = &request_msg;
  
  message->len = logi__device__proto__usb_msg__get_packed_size(&usb_msg);
  message->data = g_malloc(message->len);
  
  size_t packed_len = logi__device__proto__usb_msg__pack(&usb_msg, message->data);
  return packed_len == message->len?0:1;
}

/*!
 * @brief Generate a TransitionToDeviceMode request
 * @param message - struct that will contain the message data.  The caller have to free the data after use.
 * @return 0 if success, otherwise the error code
 */
int proto_manager_generate_transition_to_device_mode_request(Message* message) {
  if (!message)
    return 1;
  Logi__Device__Proto__Header header_msg = LOGI__DEVICE__PROTO__HEADER__INIT;
  Logi__Device__Proto__TransitionToDeviceModeRequest transition_to_device_mode_msg = LOGI__DEVICE__PROTO__TRANSITION_TO_DEVICE_MODE_REQUEST__INIT;
  
  proto_manager_set_header(&header_msg);
  
  Logi__Device__Proto__Request request_msg = {PROTOBUF_C_MESSAGE_INIT(&logi__device__proto__request__descriptor), LOGI__DEVICE__PROTO__REQUEST__PAYLOAD_TRANSITION_TO_DEVICEMODE_REQUEST, {&transition_to_device_mode_msg}};
  
  Logi__Device__Proto__UsbMsg usb_msg = LOGI__DEVICE__PROTO__USB_MSG__INIT;
  usb_msg.header = &header_msg;
  usb_msg.message_case = LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_REQUEST;
  usb_msg.request = &request_msg;
  
  message->len = logi__device__proto__usb_msg__get_packed_size(&usb_msg);
  message->data = g_malloc(message->len);
  
  size_t packed_len = logi__device__proto__usb_msg__pack(&usb_msg, message->data);
  return packed_len == message->len?0:1;
}

/*!
* @brief Decode the incoming message
* @param data - contains the incoming message to be parsed
* @param len - size of the array pointer message
* @param decoded_data - decoded data
* @return the proto id
*/
Proto_id proto_manager_decode_message(const gchar* data, guint32 len, Decoded_Data* decoded_data) {
  Proto_id proto_id = kProtoId_UnknownId;

  Logi__Device__Proto__UsbMsg* usb_msg = logi__device__proto__usb_msg__unpack(NULL,len, data);

  if (usb_msg == NULL) {
	  g_printf("Unable to unpack data\n");
	  return proto_id;
  }

  switch (usb_msg->message_case) {
  case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_ACK:
	  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_ACK\n");
	  proto_id = kProtoId_Ack;
	  break;
  case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_RESPONSE:
  {
	  if (!usb_msg->response)
		  return proto_id;
	  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_RESPONSE\n");
	  switch (usb_msg->response->payload_case) {
	  case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_GET_DEVICE_INFO_RESPONSE:
		  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_GET_DEVICE_INFO_RESPONSE\n");
		 if (usb_msg->response->get_device_info_response) {
			 proto_id = kProtoId_GetDeviceInfoResponse;
			 if (usb_msg->response->get_device_info_response->payload) {
				 g_stpcpy((gchar*)&decoded_data->device_info, usb_msg->response->get_device_info_response->payload);
			 }
		 }
		  break;
	  case LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_TRANSITION_TO_DEVICEMODE_RESPONSE:
		  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__RESPONSE__PAYLOAD_TRANSITION_TO_DEVICEMODE_RESPONSE\n");
			 if (usb_msg->response->transition_to_devicemode_response) {
				 proto_id = kProtoId_TransitionToDeviceModeResponse;
				 decoded_data->transition_to_device_mode = usb_msg->response->transition_to_devicemode_response->success;
			 }
		  break;
	  default:
		  g_printf("[proto_manager_decode_message] - Unhandled response %d\n", usb_msg->response->payload_case);
		  break;
	  };
  }
	  break;
  case LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_EVENT:
  {
	  if (!usb_msg->event)
		  return proto_id;
	  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__USB_MSG__MESSAGE_EVENT\n");
	  switch (usb_msg->event->payload_case) {
	  case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_KONG_EVENT:
	  {
		  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__EVENT__PAYLOAD_KONG_EVENT\n");
		 if (usb_msg->event->kong_event) {
			 proto_id = kProtoId_KongEvent;
			 if (usb_msg->event->kong_event->mqtt_event) {
				 g_stpcpy((gchar*)&decoded_data->device_info, usb_msg->event->kong_event->mqtt_event);
			 }
		 }

	  }
		  break;
	  case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_HANDSHAKE_EVENT:
	  {
		  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__EVENT__PAYLOAD_HANDSHAKE_EVENT\n");
		 if (usb_msg->event->handshake_event) {
			 proto_id = kProtoId_HandshakeEvent;
			 decoded_data->hand_shake_event = TRUE;
		 }
	  }
		  break;
	  case LOGI__DEVICE__PROTO__EVENT__PAYLOAD_CRASH_DUMP_AVAILABLE_EVENT:
	  {
		  g_printf("[proto_manager_decode_message] - LOGI__DEVICE__PROTO__EVENT__PAYLOAD_CRASH_DUMP_AVAILABLE_EVENT\n");
		  proto_id = kProtoId_CrashDumpAvailableEvent;
	  }
		  break;
	  default:
		  g_printf("[proto_manager_decode_message] - Unhandled event %d\n", usb_msg->event->payload_case);
		  break;
	  };
  }
	  break;
  default:
	  g_printf("[proto_manager_decode_message] - Unknown\n");
	  break;
  };
  logi__device__proto__usb_msg__free_unpacked(usb_msg,NULL);
  return proto_id;
}

/*!
* @brief Parse the device info
* @param data - string containing the device info
* @param dev_info - contains the device information
* @return true if parsing is successful
*/
gboolean proto_manager_parse_device_info(const gchar* data, device_info* dev_info) {
return TRUE;
}







