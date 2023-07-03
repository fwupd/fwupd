/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

/*
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#define HIDPP_REPORT_NOTIFICATION 0x01

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
 * Bolt registers
 */
#define BOLT_REGISTER_HIDPP_REPORTING	       0x00
#define BOLT_REGISTER_CONNECTION_STATE	       0x02
#define BOLT_REGISTER_DEVICE_ACTIVITY	       0xB3
#define BOLT_REGISTER_PAIRING_INFORMATION      0xB5
#define BOLT_REGISTER_PERFORM_DEVICE_DISCOVERY 0xC0
#define BOLT_REGISTER_PERFORM_DEVICE_PAIRING   0xC1
#define BOLT_REGISTER_RESET		       0xF2
#define BOLT_REGISTER_RECEIVER_FW_INFORMATION  0xF4
#define BOLT_REGISTER_DFU_CONTROL	       0xF5
#define BOLT_REGISTER_UNIQUE_IDENTIFIER	       0xFB

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

#include "fu-logitech-hidpp-hidpp-msg.h"

gboolean
fu_logitech_hidpp_send(FuIOChannel *io_channel,
		       FuLogitechHidppHidppMsg *msg,
		       guint timeout,
		       GError **error);
gboolean
fu_logitech_hidpp_receive(FuIOChannel *io_channel,
			  FuLogitechHidppHidppMsg *msg,
			  guint timeout,
			  GError **error);
gboolean
fu_logitech_hidpp_transfer(FuIOChannel *io_channel, FuLogitechHidppHidppMsg *msg, GError **error);
