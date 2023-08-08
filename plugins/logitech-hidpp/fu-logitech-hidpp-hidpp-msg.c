/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-hidpp-msg.h"
#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-struct.h"

FuLogitechHidppHidppMsg *
fu_logitech_hidpp_msg_new(void)
{
	return g_new0(FuLogitechHidppHidppMsg, 1);
}

gsize
fu_logitech_hidpp_msg_get_payload_length(FuLogitechHidppHidppMsg *msg)
{
	if (msg->report_id == FU_LOGITECH_HIDPP_REPORT_ID_SHORT)
		return 0x07;
	if (msg->report_id == FU_LOGITECH_HIDPP_REPORT_ID_LONG)
		return 0x14;
	if (msg->report_id == FU_LOGITECH_HIDPP_REPORT_ID_VERY_LONG)
		return 0x2f;
	if (msg->report_id == HIDPP_REPORT_NOTIFICATION)
		return 0x08;
	return 0x0;
}

const gchar *
fu_logitech_hidpp_msg_fcn_id_to_string(FuLogitechHidppHidppMsg *msg)
{
	g_return_val_if_fail(msg != NULL, NULL);
	switch (msg->sub_id) {
	case FU_LOGITECH_HIDPP_SUBID_SET_REGISTER:
	case FU_LOGITECH_HIDPP_SUBID_GET_REGISTER:
	case FU_LOGITECH_HIDPP_SUBID_SET_LONG_REGISTER:
	case FU_LOGITECH_HIDPP_SUBID_GET_LONG_REGISTER:
	case FU_LOGITECH_HIDPP_SUBID_SET_VERY_LONG_REGISTER:
	case FU_LOGITECH_HIDPP_SUBID_GET_VERY_LONG_REGISTER:
		return fu_logitech_hidpp_register_to_string(msg->function_id);
		break;
	default:
		break;
	}
	return NULL;
}

gboolean
fu_logitech_hidpp_msg_is_reply(FuLogitechHidppHidppMsg *msg1, FuLogitechHidppHidppMsg *msg2)
{
	g_return_val_if_fail(msg1 != NULL, FALSE);
	g_return_val_if_fail(msg2 != NULL, FALSE);
	if (msg1->device_id != msg2->device_id &&
	    msg1->device_id != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    msg2->device_id != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED)
		return FALSE;
	if (msg1->flags & FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SUB_ID ||
	    msg2->flags & FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SUB_ID)
		return TRUE;
	if (msg1->sub_id != msg2->sub_id)
		return FALSE;
	if (msg1->flags & FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_FNCT_ID ||
	    msg2->flags & FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_FNCT_ID)
		return TRUE;
	if (msg1->function_id != msg2->function_id)
		return FALSE;
	return TRUE;
}

/* HID++ error */
gboolean
fu_logitech_hidpp_msg_is_error(FuLogitechHidppHidppMsg *msg, GError **error)
{
	g_return_val_if_fail(msg != NULL, FALSE);
	if (msg->sub_id == FU_LOGITECH_HIDPP_SUBID_ERROR_MSG) {
		switch (msg->data[1]) {
		case HIDPP_ERR_INVALID_SUBID:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    "invalid SubID");
			break;
		case HIDPP_ERR_INVALID_ADDRESS:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "invalid address");
			break;
		case HIDPP_ERR_INVALID_VALUE:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "invalid value");
			break;
		case HIDPP_ERR_CONNECT_FAIL:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "connection request failed");
			break;
		case HIDPP_ERR_TOO_MANY_DEVICES:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NO_SPACE,
					    "too many devices connected");
			break;
		case HIDPP_ERR_ALREADY_EXISTS:
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "already exists");
			break;
		case HIDPP_ERR_BUSY:
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "busy");
			break;
		case HIDPP_ERR_UNKNOWN_DEVICE:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "unknown device");
			break;
		case HIDPP_ERR_RESOURCE_ERROR:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_HOST_UNREACHABLE,
					    "resource error");
			break;
		case HIDPP_ERR_REQUEST_UNAVAILABLE:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_EXISTS,
					    "request not valid in current context");
			break;
		case HIDPP_ERR_INVALID_PARAM_VALUE:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "request parameter has unsupported value");
			break;
		case HIDPP_ERR_WRONG_PIN_CODE:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_CONNECTION_REFUSED,
					    "the pin code was wrong");
			break;
		default:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "generic failure");
		}
		return FALSE;
	}
	if (msg->sub_id == FU_LOGITECH_HIDPP_SUBID_ERROR_MSG_20) {
		switch (msg->data[1]) {
		case HIDPP_ERROR_CODE_INVALID_ARGUMENT:
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "Invalid argument 0x%02x",
				    msg->data[2]);
			break;
		case HIDPP_ERROR_CODE_OUT_OF_RANGE:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "out of range");
			break;
		case HIDPP_ERROR_CODE_HW_ERROR:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_BROKEN_PIPE,
					    "hardware error");
			break;
		case HIDPP_ERROR_CODE_INVALID_FEATURE_INDEX:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_ARGUMENT,
					    "invalid feature index");
			break;
		case HIDPP_ERROR_CODE_INVALID_FUNCTION_ID:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_ARGUMENT,
					    "invalid function ID");
			break;
		case HIDPP_ERROR_CODE_BUSY:
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "busy");
			break;
		case HIDPP_ERROR_CODE_UNSUPPORTED:
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    "unsupported");
			break;
		default:
			g_set_error_literal(error,
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
fu_logitech_hidpp_msg_copy(FuLogitechHidppHidppMsg *msg_dst, const FuLogitechHidppHidppMsg *msg_src)
{
	g_return_if_fail(msg_dst != NULL);
	g_return_if_fail(msg_src != NULL);
	memset(msg_dst->data, 0x00, sizeof(msg_dst->data));
	msg_dst->device_id = msg_src->device_id;
	msg_dst->sub_id = msg_src->sub_id;
	msg_dst->function_id = msg_src->function_id;
	memcpy(msg_dst->data, msg_src->data, sizeof(msg_dst->data));
}

/* filter HID++1.0 messages */
gboolean
fu_logitech_hidpp_msg_is_hidpp10_compat(FuLogitechHidppHidppMsg *msg)
{
	g_return_val_if_fail(msg != NULL, FALSE);
	if (msg->sub_id == 0x40 || msg->sub_id == 0x41 || msg->sub_id == 0x49 ||
	    msg->sub_id == 0x4b || msg->sub_id == 0x8f) {
		return TRUE;
	}
	return FALSE;
}

gboolean
fu_logitech_hidpp_msg_verify_swid(FuLogitechHidppHidppMsg *msg)
{
	g_return_val_if_fail(msg != NULL, FALSE);
	if ((msg->function_id & 0x0f) != FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID)
		return FALSE;
	return TRUE;
}
