/*
 * Copyright 2024 Huddly
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-huddly-usb-common.h"

void
fu_huddly_usb_hlink_msg_free(FuHuddlyUsbHLinkMsg *msg)
{
	g_free(msg->msg_name);
	if (msg->header != NULL)
		g_byte_array_unref(msg->header);
	if (msg->payload != NULL)
		g_byte_array_unref(msg->payload);
	g_free(msg);
}

FuHuddlyUsbHLinkMsg *
fu_huddly_usb_hlink_msg_new(const gchar *msg_name, GByteArray *payload)
{
	g_autoptr(FuHuddlyUsbHLinkMsg) msg = g_new0(FuHuddlyUsbHLinkMsg, 1);

	g_return_val_if_fail(msg_name != NULL, NULL);

	msg->header = fu_struct_h_link_header_new();
	msg->msg_name = g_strdup(msg_name);
	fu_struct_h_link_header_set_msg_name_size(msg->header, strlen(msg_name));
	if (payload != NULL) {
		fu_struct_h_link_header_set_payload_size(msg->header, payload->len);
		msg->payload = g_byte_array_ref(payload);
	}

	return g_steal_pointer(&msg);
}

FuHuddlyUsbHLinkMsg *
fu_huddly_usb_hlink_msg_new_string(const gchar *msg_name, const gchar *payload)
{
	g_autoptr(GByteArray) payload_buf = g_byte_array_new();

	g_return_val_if_fail(msg_name != NULL, NULL);
	g_return_val_if_fail(payload != NULL, NULL);

	g_byte_array_append(payload_buf, (const guint8 *)payload, strlen(payload));
	return fu_huddly_usb_hlink_msg_new(msg_name, payload_buf);
}

GByteArray *
fu_huddly_usb_hlink_msg_write(FuHuddlyUsbHLinkMsg *msg, GError **error)
{
	g_autoptr(GByteArray) packet = g_byte_array_new();

	g_return_val_if_fail(msg != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	g_byte_array_append(packet, msg->header->data, msg->header->len);
	g_byte_array_append(packet, (const guint8 *)msg->msg_name, strlen(msg->msg_name));
	if (msg->payload != NULL)
		g_byte_array_append(packet, msg->payload->data, msg->payload->len);
	return g_steal_pointer(&packet);
}

FuHuddlyUsbHLinkMsg *
fu_huddly_usb_hlink_msg_parse(const guint8 *buf, gsize bufsz, GError **error)
{
	gsize offset = 0;
	guint16 msg_name_size;
	guint32 payload_size;
	g_autoptr(FuHuddlyUsbHLinkMsg) msg = g_new0(FuHuddlyUsbHLinkMsg, 1);

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	msg->header = fu_struct_h_link_header_parse(buf, bufsz, 0x0, error);
	if (msg->header == NULL)
		return NULL;

	offset += FU_STRUCT_H_LINK_HEADER_SIZE;
	msg_name_size = fu_struct_h_link_header_get_msg_name_size(msg->header);
	if (msg_name_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "HLink message had no message name");
		return NULL;
	}
	msg->msg_name = g_new0(gchar, msg_name_size + 1);
	if (!fu_memcpy_safe((guint8 *)msg->msg_name,
			    msg_name_size,
			    0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    msg_name_size,
			    error)) {
		return NULL;
	}

	offset += msg_name_size;
	payload_size = fu_struct_h_link_header_get_payload_size(msg->header);
	msg->payload = g_byte_array_sized_new(payload_size);
	g_byte_array_set_size(msg->payload, payload_size);
	if (!fu_memcpy_safe(msg->payload->data,
			    msg->payload->len,
			    0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    payload_size,
			    error)) {
		return NULL;
	}

	/* success */
	return g_steal_pointer(&msg);
}
