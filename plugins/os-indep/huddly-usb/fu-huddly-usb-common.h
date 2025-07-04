/*
 * Copyright 2024 Huddly
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-huddly-usb-struct.h"

typedef struct {
	FuStructHLinkHeader *header;
	gchar *msg_name;
	GByteArray *payload; /* nullable */
} FuHuddlyUsbHLinkMsg;

FuHuddlyUsbHLinkMsg *
fu_huddly_usb_hlink_msg_new(const gchar *msg_name, GByteArray *payload);
FuHuddlyUsbHLinkMsg *
fu_huddly_usb_hlink_msg_new_string(const gchar *msg_name, const gchar *payload);
GByteArray *
fu_huddly_usb_hlink_msg_write(FuHuddlyUsbHLinkMsg *msg, GError **error);
FuHuddlyUsbHLinkMsg *
fu_huddly_usb_hlink_msg_parse(const guint8 *buf, gsize bufsz, GError **error);

void
fu_huddly_usb_hlink_msg_free(FuHuddlyUsbHLinkMsg *msg);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuHuddlyUsbHLinkMsg, fu_huddly_usb_hlink_msg_free)
