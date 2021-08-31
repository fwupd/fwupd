/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <gio/gio.h>

/*
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */
#define HIDPP_DEVICE_ID_WIRED	 0x00
#define HIDPP_DEVICE_ID_RECEIVER 0xFF
#define HIDPP_DEVICE_ID_UNSET	 0xFE

#define HIDPP_REPORT_NOTIFICATION 0x01
#define HIDPP_REPORT_ID_SHORT	  0x10
#define HIDPP_REPORT_ID_LONG	  0x11
#define HIDPP_REPORT_ID_VERY_LONG 0x12

#define HIDPP_SUBID_VENDOR_SPECIFIC_KEYS      0x03
#define HIDPP_SUBID_POWER_KEYS		      0x04
#define HIDPP_SUBID_ROLLER		      0x05
#define HIDPP_SUBID_MOUSE_EXTRA_BUTTONS	      0x06
#define HIDPP_SUBID_BATTERY_CHARGING_LEVEL    0x07
#define HIDPP_SUBID_USER_INTERFACE_EVENT      0x08
#define HIDPP_SUBID_F_LOCK_STATUS	      0x09
#define HIDPP_SUBID_CALCULATOR_RESULT	      0x0A
#define HIDPP_SUBID_MENU_NAVIGATE	      0x0B
#define HIDPP_SUBID_FN_KEY		      0x0C
#define HIDPP_SUBID_BATTERY_MILEAGE	      0x0D
#define HIDPP_SUBID_UART_RX		      0x0E
#define HIDPP_SUBID_BACKLIGHT_DURATION_UPDATE 0x17
#define HIDPP_SUBID_DEVICE_DISCONNECTION      0x40
#define HIDPP_SUBID_DEVICE_CONNECTION	      0x41
#define HIDPP_SUBID_DEVICE_DISCOVERY	      0x42
#define HIDPP_SUBID_PIN_CODE_REQUEST	      0x43
#define HIDPP_SUBID_RECEIVER_WORKING_MODE     0x44
#define HIDPP_SUBID_ERROR_MESSAGE	      0x45
#define HIDPP_SUBID_RF_LINK_CHANGE	      0x46
#define HIDPP_SUBID_HCI			      0x48
#define HIDPP_SUBID_LINK_QUALITY	      0x49
#define HIDPP_SUBID_DEVICE_LOCKING_CHANGED    0x4a
#define HIDPP_SUBID_WIRELESS_DEVICE_CHANGE    0x4B
#define HIDPP_SUBID_ACL			      0x51
#define HIDPP_SUBID_VOIP_TELEPHONY_EVENT      0x5B
#define HIDPP_SUBID_LED			      0x60
#define HIDPP_SUBID_GESTURE_AND_AIR	      0x65
#define HIDPP_SUBID_TOUCHPAD_MULTI_TOUCH      0x66
#define HIDPP_SUBID_TRACEABILITY	      0x78
#define HIDPP_SUBID_SET_REGISTER	      0x80
#define HIDPP_SUBID_GET_REGISTER	      0x81
#define HIDPP_SUBID_SET_LONG_REGISTER	      0x82
#define HIDPP_SUBID_GET_LONG_REGISTER	      0x83
#define HIDPP_SUBID_SET_VERY_LONG_REGISTER    0x84
#define HIDPP_SUBID_GET_VERY_LONG_REGISTER    0x85
#define HIDPP_SUBID_ERROR_MSG		      0x8F
#define HIDPP_SUBID_ERROR_MSG_20	      0xFF

#define HIDPP_ERR_SUCCESS	      0x00
#define HIDPP_ERR_INVALID_SUBID	      0x01
#define HIDPP_ERR_INVALID_ADDRESS     0x02
#define HIDPP_ERR_INVALID_VALUE	      0x03
#define HIDPP_ERR_CONNECT_FAIL	      0x04
#define HIDPP_ERR_TOO_MANY_DEVICES    0x05
#define HIDPP_ERR_ALREADY_EXISTS      0x06
#define HIDPP_ERR_BUSY		      0x07
#define HIDPP_ERR_UNKNOWN_DEVICE      0x08
#define HIDPP_ERR_RESOURCE_ERROR      0x09
#define HIDPP_ERR_REQUEST_UNAVAILABLE 0x0A
#define HIDPP_ERR_INVALID_PARAM_VALUE 0x0B
#define HIDPP_ERR_WRONG_PIN_CODE      0x0C

/*
 * HID++1.0 registers
 */

#define HIDPP_REGISTER_HIDPP_NOTIFICATIONS	       0x00
#define HIDPP_REGISTER_ENABLE_INDIVIDUAL_FEATURES      0x01
#define HIDPP_REGISTER_BATTERY_STATUS		       0x07
#define HIDPP_REGISTER_BATTERY_MILEAGE		       0x0D
#define HIDPP_REGISTER_PROFILE			       0x0F
#define HIDPP_REGISTER_LED_STATUS		       0x51
#define HIDPP_REGISTER_LED_INTENSITY		       0x54
#define HIDPP_REGISTER_LED_COLOR		       0x57
#define HIDPP_REGISTER_OPTICAL_SENSOR_SETTINGS	       0x61
#define HIDPP_REGISTER_CURRENT_RESOLUTION	       0x63
#define HIDPP_REGISTER_USB_REFRESH_RATE		       0x64
#define HIDPP_REGISTER_GENERIC_MEMORY_MANAGEMENT       0xA0
#define HIDPP_REGISTER_HOT_CONTROL		       0xA1
#define HIDPP_REGISTER_READ_MEMORY		       0xA2
#define HIDPP_REGISTER_DEVICE_CONNECTION_DISCONNECTION 0xB2
#define HIDPP_REGISTER_PAIRING_INFORMATION	       0xB5
#define HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE     0xF0
#define HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION     0xF1

/*
 * HID++2.0 error codes
 */
#define HIDPP_ERROR_CODE_NO_ERROR	       0x00
#define HIDPP_ERROR_CODE_UNKNOWN	       0x01
#define HIDPP_ERROR_CODE_INVALID_ARGUMENT      0x02
#define HIDPP_ERROR_CODE_OUT_OF_RANGE	       0x03
#define HIDPP_ERROR_CODE_HW_ERROR	       0x04
#define HIDPP_ERROR_CODE_LOGITECH_INTERNAL     0x05
#define HIDPP_ERROR_CODE_INVALID_FEATURE_INDEX 0x06
#define HIDPP_ERROR_CODE_INVALID_FUNCTION_ID   0x07
#define HIDPP_ERROR_CODE_BUSY		       0x08
#define HIDPP_ERROR_CODE_UNSUPPORTED	       0x09

/*
 * HID++2.0 features
 */
#define HIDPP_FEATURE_ROOT		      0x0000
#define HIDPP_FEATURE_I_FEATURE_SET	      0x0001
#define HIDPP_FEATURE_I_FIRMWARE_INFO	      0x0003
#define HIDPP_FEATURE_GET_DEVICE_NAME_TYPE    0x0005
#define HIDPP_FEATURE_DFU_CONTROL	      0x00c1
#define HIDPP_FEATURE_DFU_CONTROL_SIGNED      0x00c2
#define HIDPP_FEATURE_DFU		      0x00d0
#define HIDPP_FEATURE_BATTERY_LEVEL_STATUS    0x1000
#define HIDPP_FEATURE_UNIFIED_BATTERY	      0x1004
#define HIDPP_FEATURE_KBD_REPROGRAMMABLE_KEYS 0x1b00
#define HIDPP_FEATURE_SPECIAL_KEYS_BUTTONS    0x1b04
#define HIDPP_FEATURE_MOUSE_POINTER_BASIC     0x2200
#define HIDPP_FEATURE_ADJUSTABLE_DPI	      0x2201
#define HIDPP_FEATURE_ADJUSTABLE_REPORT_RATE  0x8060
#define HIDPP_FEATURE_COLOR_LED_EFFECTS	      0x8070
#define HIDPP_FEATURE_ONBOARD_PROFILES	      0x8100
#define HIDPP_FEATURE_MOUSE_BUTTON_SPY	      0x8110

#include "fu-logitech-hidpp-hidpp-msg.h"

gboolean
fu_logitech_hidpp_send(FuIOChannel *self,
		       FuLogitechHidPpHidppMsg *msg,
		       guint timeout,
		       GError **error);
gboolean
fu_logitech_hidpp_receive(FuIOChannel *self,
			  FuLogitechHidPpHidppMsg *msg,
			  guint timeout,
			  GError **error);
gboolean
fu_logitech_hidpp_transfer(FuIOChannel *self, FuLogitechHidPpHidppMsg *msg, GError **error);
