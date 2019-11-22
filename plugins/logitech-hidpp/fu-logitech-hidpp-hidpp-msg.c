/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-hidpp-msg.h"

FuLogitechHidPpHidppMsg *
fu_logitech_hidpp_msg_new (void)
{
	return g_new0 (FuLogitechHidPpHidppMsg, 1);
}

const gchar *
fu_logitech_hidpp_msg_dev_id_to_string (FuLogitechHidPpHidppMsg *msg)
{
	g_return_val_if_fail (msg != NULL, NULL);
	if (msg->device_id == HIDPP_DEVICE_ID_WIRED)
		return "wired";
	if (msg->device_id == HIDPP_DEVICE_ID_RECEIVER)
		return "receiver";
	if (msg->device_id == HIDPP_DEVICE_ID_UNSET)
		return "unset";
	return NULL;
}

const gchar *
fu_logitech_hidpp_msg_rpt_id_to_string (FuLogitechHidPpHidppMsg *msg)
{
	g_return_val_if_fail (msg != NULL, NULL);
	if (msg->report_id == HIDPP_REPORT_ID_SHORT)
		return "short";
	if (msg->report_id == HIDPP_REPORT_ID_LONG)
		return "long";
	if (msg->report_id == HIDPP_REPORT_ID_VERY_LONG)
		return "very-long";
	return NULL;
}

gsize
fu_logitech_hidpp_msg_get_payload_length (FuLogitechHidPpHidppMsg *msg)
{
	if (msg->report_id == HIDPP_REPORT_ID_SHORT)
		return 0x07;
	if (msg->report_id == HIDPP_REPORT_ID_LONG)
		return 0x14;
	if (msg->report_id == HIDPP_REPORT_ID_VERY_LONG)
		return 0x2f;
	if (msg->report_id == HIDPP_REPORT_NOTIFICATION)
		return 0x08;
	return 0x0;
}

const gchar *
fu_logitech_hidpp_msg_fcn_id_to_string (FuLogitechHidPpHidppMsg *msg)
{
	g_return_val_if_fail (msg != NULL, NULL);
	switch (msg->sub_id) {
	case HIDPP_SUBID_SET_REGISTER:
	case HIDPP_SUBID_GET_REGISTER:
	case HIDPP_SUBID_SET_LONG_REGISTER:
	case HIDPP_SUBID_GET_LONG_REGISTER:
	case HIDPP_SUBID_SET_VERY_LONG_REGISTER:
	case HIDPP_SUBID_GET_VERY_LONG_REGISTER:
		if (msg->function_id == HIDPP_REGISTER_HIDPP_NOTIFICATIONS)
			return "hidpp-notifications";
		if (msg->function_id == HIDPP_REGISTER_ENABLE_INDIVIDUAL_FEATURES)
			return "individual-features";
		if (msg->function_id == HIDPP_REGISTER_BATTERY_STATUS)
			return "battery-status";
		if (msg->function_id == HIDPP_REGISTER_BATTERY_MILEAGE)
			return "battery-mileage";
		if (msg->function_id == HIDPP_REGISTER_PROFILE)
			return "profile";
		if (msg->function_id == HIDPP_REGISTER_LED_STATUS)
			return "led-status";
		if (msg->function_id == HIDPP_REGISTER_LED_INTENSITY)
			return "led-intensity";
		if (msg->function_id == HIDPP_REGISTER_LED_COLOR)
			return "led-color";
		if (msg->function_id == HIDPP_REGISTER_OPTICAL_SENSOR_SETTINGS)
			return "optical-sensor-settings";
		if (msg->function_id == HIDPP_REGISTER_CURRENT_RESOLUTION)
			return "current-resolution";
		if (msg->function_id == HIDPP_REGISTER_USB_REFRESH_RATE)
			return "usb-refresh-rate";
		if (msg->function_id == HIDPP_REGISTER_GENERIC_MEMORY_MANAGEMENT)
			return "generic-memory-management";
		if (msg->function_id == HIDPP_REGISTER_HOT_CONTROL)
			return "hot-control";
		if (msg->function_id == HIDPP_REGISTER_READ_MEMORY)
			return "read-memory";
		if (msg->function_id == HIDPP_REGISTER_DEVICE_CONNECTION_DISCONNECTION)
			return "device-connection-disconnection";
		if (msg->function_id == HIDPP_REGISTER_PAIRING_INFORMATION)
			return "pairing-information";
		if (msg->function_id == HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE)
			return "device-firmware-update-mode";
		if (msg->function_id == HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION)
			return "device-firmware-information";
		break;
	default:
		break;
	}
	return NULL;

}

const gchar *
fu_logitech_hidpp_msg_sub_id_to_string (FuLogitechHidPpHidppMsg *msg)
{
	g_return_val_if_fail (msg != NULL, NULL);
	if (msg->sub_id == HIDPP_SUBID_VENDOR_SPECIFIC_KEYS)
		return "vendor-specific-keys";
	if (msg->sub_id == HIDPP_SUBID_POWER_KEYS)
		return "power-keys";
	if (msg->sub_id == HIDPP_SUBID_ROLLER)
		return "roller";
	if (msg->sub_id == HIDPP_SUBID_MOUSE_EXTRA_BUTTONS)
		return "mouse-extra-buttons";
	if (msg->sub_id == HIDPP_SUBID_BATTERY_CHARGING_LEVEL)
		return "battery-charging-level";
	if (msg->sub_id == HIDPP_SUBID_USER_INTERFACE_EVENT)
		return "user-interface-event";
	if (msg->sub_id == HIDPP_SUBID_F_LOCK_STATUS)
		return "f-lock-status";
	if (msg->sub_id == HIDPP_SUBID_CALCULATOR_RESULT)
		return "calculator-result";
	if (msg->sub_id == HIDPP_SUBID_MENU_NAVIGATE)
		return "menu-navigate";
	if (msg->sub_id == HIDPP_SUBID_FN_KEY)
		return "fn-key";
	if (msg->sub_id == HIDPP_SUBID_BATTERY_MILEAGE)
		return "battery-mileage";
	if (msg->sub_id == HIDPP_SUBID_UART_RX)
		return "uart-rx";
	if (msg->sub_id == HIDPP_SUBID_BACKLIGHT_DURATION_UPDATE)
		return "backlight-duration-update";
	if (msg->sub_id == HIDPP_SUBID_DEVICE_DISCONNECTION)
		return "device-disconnection";
	if (msg->sub_id == HIDPP_SUBID_DEVICE_CONNECTION)
		return "device-connection";
	if (msg->sub_id == HIDPP_SUBID_DEVICE_DISCOVERY)
		return "device-discovery";
	if (msg->sub_id == HIDPP_SUBID_PIN_CODE_REQUEST)
		return "pin-code-request";
	if (msg->sub_id == HIDPP_SUBID_RECEIVER_WORKING_MODE)
		return "receiver-working-mode";
	if (msg->sub_id == HIDPP_SUBID_ERROR_MESSAGE)
		return "error-message";
	if (msg->sub_id == HIDPP_SUBID_RF_LINK_CHANGE)
		return "rf-link-change";
	if (msg->sub_id == HIDPP_SUBID_HCI)
		return "hci";
	if (msg->sub_id == HIDPP_SUBID_LINK_QUALITY)
		return "link-quality";
	if (msg->sub_id == HIDPP_SUBID_DEVICE_LOCKING_CHANGED)
		return "device-locking-changed";
	if (msg->sub_id == HIDPP_SUBID_WIRELESS_DEVICE_CHANGE)
		return "wireless-device-change";
	if (msg->sub_id == HIDPP_SUBID_ACL)
		return "acl";
	if (msg->sub_id == HIDPP_SUBID_VOIP_TELEPHONY_EVENT)
		return "voip-telephony-event";
	if (msg->sub_id == HIDPP_SUBID_LED)
		return "led";
	if (msg->sub_id == HIDPP_SUBID_GESTURE_AND_AIR)
		return "gesture-and-air";
	if (msg->sub_id == HIDPP_SUBID_TOUCHPAD_MULTI_TOUCH)
		return "touchpad-multi-touch";
	if (msg->sub_id == HIDPP_SUBID_TRACEABILITY)
		return "traceability";
	if (msg->sub_id == HIDPP_SUBID_SET_REGISTER)
		return "set-register";
	if (msg->sub_id == HIDPP_SUBID_GET_REGISTER)
		return "get-register";
	if (msg->sub_id == HIDPP_SUBID_SET_LONG_REGISTER)
		return "set-long-register";
	if (msg->sub_id == HIDPP_SUBID_GET_LONG_REGISTER)
		return "get-long-register";
	if (msg->sub_id == HIDPP_SUBID_SET_VERY_LONG_REGISTER)
		return "set-very-long-register";
	if (msg->sub_id == HIDPP_SUBID_GET_VERY_LONG_REGISTER)
		return "get-very-long-register";
	if (msg->sub_id == HIDPP_SUBID_ERROR_MSG)
		return "error-msg";
	if (msg->sub_id == HIDPP_SUBID_ERROR_MSG_20)
		return "error-msg-v2";
	return NULL;
}

gboolean
fu_logitech_hidpp_msg_is_reply (FuLogitechHidPpHidppMsg *msg1, FuLogitechHidPpHidppMsg *msg2)
{
	g_return_val_if_fail (msg1 != NULL, FALSE);
	g_return_val_if_fail (msg2 != NULL, FALSE);
	if (msg1->device_id != msg2->device_id &&
	    msg1->device_id != HIDPP_DEVICE_ID_UNSET &&
	    msg2->device_id != HIDPP_DEVICE_ID_UNSET)
		return FALSE;
	if (msg1->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID ||
	    msg2->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID)
		return TRUE;
	if (msg1->sub_id != msg2->sub_id)
		return FALSE;
	if (msg1->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID ||
	    msg2->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID)
		return TRUE;
	if (msg1->function_id != msg2->function_id)
		return FALSE;
	return TRUE;
}

/* HID++ error */
gboolean
fu_logitech_hidpp_msg_is_error (FuLogitechHidPpHidppMsg *msg, GError **error)
{
	g_return_val_if_fail (msg != NULL, FALSE);
	if (msg->sub_id == HIDPP_SUBID_ERROR_MSG) {
		switch (msg->data[1]) {
		case HIDPP_ERR_INVALID_SUBID:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "invalid SubID");
			break;
		case HIDPP_ERR_INVALID_ADDRESS:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "invalid address");
			break;
		case HIDPP_ERR_INVALID_VALUE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "invalid value");
			break;
		case HIDPP_ERR_CONNECT_FAIL:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "connection request failed");
			break;
		case HIDPP_ERR_TOO_MANY_DEVICES:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NO_SPACE,
					     "too many devices connected");
			break;
		case HIDPP_ERR_ALREADY_EXISTS:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_EXISTS,
					     "already exists");
			break;
		case HIDPP_ERR_BUSY:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_BUSY,
					     "busy");
			break;
		case HIDPP_ERR_UNKNOWN_DEVICE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "unknown device");
			break;
		case HIDPP_ERR_RESOURCE_ERROR:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_HOST_UNREACHABLE,
					     "resource error");
			break;
		case HIDPP_ERR_REQUEST_UNAVAILABLE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_EXISTS,
					     "request not valid in current context");
			break;
		case HIDPP_ERR_INVALID_PARAM_VALUE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "request parameter has unsupported value");
			break;
		case HIDPP_ERR_WRONG_PIN_CODE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_CONNECTION_REFUSED,
					     "the pin code was wrong");
			break;
		default:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "generic failure");
		}
		return FALSE;
	}
	if (msg->sub_id == HIDPP_SUBID_ERROR_MSG_20) {
		switch (msg->data[1]) {
		case HIDPP_ERROR_CODE_INVALID_ARGUMENT:
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_ARGUMENT,
				     "Invalid argument 0x%02x",
				     msg->data[2]);
			break;
		case HIDPP_ERROR_CODE_OUT_OF_RANGE:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "out of range");
			break;
		case HIDPP_ERROR_CODE_HW_ERROR:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_BROKEN_PIPE,
					     "hardware error");
			break;
		case HIDPP_ERROR_CODE_INVALID_FEATURE_INDEX:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "invalid feature index");
			break;
		case HIDPP_ERROR_CODE_INVALID_FUNCTION_ID:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "invalid function ID");
			break;
		case HIDPP_ERROR_CODE_BUSY:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_BUSY,
					     "busy");
			break;
		case HIDPP_ERROR_CODE_UNSUPPORTED:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "unsupported");
			break;
		default:
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "generic failure");
			break;
		}
		return FALSE;
	}
	return TRUE;
}

void
fu_logitech_hidpp_msg_copy (FuLogitechHidPpHidppMsg *msg_dst, const FuLogitechHidPpHidppMsg *msg_src)
{
	g_return_if_fail (msg_dst != NULL);
	g_return_if_fail (msg_src != NULL);
	memset (msg_dst->data, 0x00, sizeof(msg_dst->data));
	msg_dst->device_id = msg_src->device_id;
	msg_dst->sub_id = msg_src->sub_id;
	msg_dst->function_id = msg_src->function_id;
	memcpy (msg_dst->data, msg_src->data, sizeof(msg_dst->data));
}

/* filter HID++1.0 messages */
gboolean
fu_logitech_hidpp_msg_is_hidpp10_compat (FuLogitechHidPpHidppMsg *msg)
{
	g_return_val_if_fail (msg != NULL, FALSE);
	if (msg->sub_id == 0x40 ||
	    msg->sub_id == 0x41 ||
	    msg->sub_id == 0x49 ||
	    msg->sub_id == 0x4b ||
	    msg->sub_id == 0x8f) {
		return TRUE;
	}
	return FALSE;
}

gboolean
fu_logitech_hidpp_msg_verify_swid (FuLogitechHidPpHidppMsg *msg)
{
	g_return_val_if_fail (msg != NULL, FALSE);
	if ((msg->function_id & 0x0f) != FU_UNIFYING_HIDPP_MSG_SW_ID)
		return FALSE;
	return TRUE;
}
