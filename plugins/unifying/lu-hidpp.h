/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __LU__HIDPP_H
#define __LU__HIDPP_H

G_BEGIN_DECLS

#define LU_REQUEST_SET_REPORT		0x09

/*
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */
#define HIDPP_DEVICE_ID_WIRED			0x00
#define HIDPP_DEVICE_ID_RECEIVER		0xFF
#define HIDPP_DEVICE_ID_UNSET			0xFE

#define HIDPP_REPORT_ID_SHORT			0x10
#define HIDPP_REPORT_ID_LONG			0x11

#define HIDPP_SUBID_DEVICE_DISCONNECTION	0x40
#define HIDPP_SUBID_DEVICE_CONNECTION		0x41
#define HIDPP_SUBID_DEVICE_LOCKING_CHANGED	0x4a
#define HIDPP_SUBID_SET_REGISTER		0x80
#define HIDPP_SUBID_GET_REGISTER		0x81
#define HIDPP_SUBID_SET_LONG_REGISTER		0x82
#define HIDPP_SUBID_GET_LONG_REGISTER		0x83
#define HIDPP_SUBID_ERROR_MSG			0x8F

#define HIDPP_ERR_SUCCESS			0x00
#define HIDPP_ERR_INVALID_SUBID			0x01
#define HIDPP_ERR_INVALID_ADDRESS		0x02
#define HIDPP_ERR_INVALID_VALUE			0x03
#define HIDPP_ERR_CONNECT_FAIL			0x04
#define HIDPP_ERR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERR_ALREADY_EXISTS		0x06
#define HIDPP_ERR_BUSY				0x07
#define HIDPP_ERR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERR_RESOURCE_ERROR		0x09
#define HIDPP_ERR_REQUEST_UNAVAILABLE		0x0A
#define HIDPP_ERR_INVALID_PARAM_VALUE		0x0B
#define HIDPP_ERR_WRONG_PIN_CODE		0x0C

/*
 * HID++1.0 registers
 */

#define HIDPP_REGISTER_HIDPP_NOTIFICATIONS			0x00
#define HIDPP_REGISTER_ENABLE_INDIVIDUAL_FEATURES		0x01
#define HIDPP_REGISTER_BATTERY_STATUS				0x07
#define HIDPP_REGISTER_BATTERY_MILEAGE				0x0D
#define HIDPP_REGISTER_PROFILE					0x0F
#define HIDPP_REGISTER_LED_STATUS				0x51
#define HIDPP_REGISTER_LED_INTENSITY				0x54
#define HIDPP_REGISTER_LED_COLOR				0x57
#define HIDPP_REGISTER_OPTICAL_SENSOR_SETTINGS			0x61
#define HIDPP_REGISTER_CURRENT_RESOLUTION			0x63
#define HIDPP_REGISTER_USB_REFRESH_RATE				0x64
#define HIDPP_REGISTER_GENERIC_MEMORY_MANAGEMENT		0xA0
#define HIDPP_REGISTER_HOT_CONTROL				0xA1
#define HIDPP_REGISTER_READ_MEMORY				0xA2
#define HIDPP_REGISTER_DEVICE_CONNECTION_DISCONNECTION		0xB2
#define HIDPP_REGISTER_PAIRING_INFORMATION			0xB5
#define HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE		0xF0
#define HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION		0xF1

/*
 * HID++2.0 pages
 */

#define HIDPP_PAGE_ROOT						0x0000
#define HIDPP_PAGE_FEATURE_SET					0x0001
#define HIDPP_PAGE_DEVICE_INFO					0x0003
#define HIDPP_PAGE_BATTERY_LEVEL_STATUS				0x1000
#define HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS			0x1b00
#define HIDPP_PAGE_SPECIAL_KEYS_BUTTONS				0x1b04
#define HIDPP_PAGE_MOUSE_POINTER_BASIC				0x2200
#define HIDPP_PAGE_ADJUSTABLE_DPI				0x2201
#define HIDPP_PAGE_ADJUSTABLE_REPORT_RATE			0x8060
#define HIDPP_PAGE_COLOR_LED_EFFECTS				0x8070
#define HIDPP_PAGE_ONBOARD_PROFILES				0x8100
#define HIDPP_PAGE_MOUSE_BUTTON_SPY				0x8110

/*
 * HID++2.0 error codes
 */
#define HIDPP_ERROR_CODE_NO_ERROR				0x00
#define HIDPP_ERROR_CODE_UNKNOWN				0x01
#define HIDPP_ERROR_CODE_INVALID_ARGUMENT			0x02
#define HIDPP_ERROR_CODE_OUT_OF_RANGE				0x03
#define HIDPP_ERROR_CODE_HW_ERROR				0x04
#define HIDPP_ERROR_CODE_LOGITECH_INTERNAL			0x05
#define HIDPP_ERROR_CODE_INVALID_FEATURE_INDEX			0x06
#define HIDPP_ERROR_CODE_INVALID_FUNCTION_ID			0x07
#define HIDPP_ERROR_CODE_BUSY					0x08
#define HIDPP_ERROR_CODE_UNSUPPORTED				0x09

/*
 * HID++2.0 features
 */
#define HIDPP_FEATURE_ROOT					0x0000
#define HIDPP_FEATURE_I_FEATURE_SET				0x0001
#define HIDPP_FEATURE_I_FIRMWARE_INFO				0x0003
#define HIDPP_FEATURE_GET_DEVICE_NAME_TYPE			0x0005
#define HIDPP_FEATURE_DFU_CONTROL				0x00c1
#define HIDPP_FEATURE_DFU_CONTROL_SIGNED			0x00c2
#define HIDPP_FEATURE_DFU					0x00d0
#define HIDPP_FEATURE_BATTERY_LEVEL_STATUS			0x1000

G_END_DECLS

#endif /* __LU__HIDPP_H */
