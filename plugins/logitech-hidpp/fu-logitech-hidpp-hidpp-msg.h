/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef enum {
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_NONE,
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LONGER_TIMEOUT = 1 << 0,
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SUB_ID = 1 << 1,
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_FNCT_ID = 1 << 2,
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_IGNORE_SWID = 1 << 3,
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_RETRY_STUCK = 1 << 4,
	/*< private >*/
	FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LAST
} FuLogitechHidppHidppMsgFlags;

typedef struct __attribute__((packed)) {
	guint8 report_id;
	guint8 device_id;
	guint8 sub_id;
	guint8 function_id; /* funcId:software_id */
	guint8 data[47];    /* maximum supported by Windows XP SP2 */
	/* not included in the packet sent to the hardware */
	guint32 flags;
	guint8 hidpp_version;
} FuLogitechHidppHidppMsg;

/* this is specific to fwupd */
#define FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID 0x07

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuLogitechHidppHidppMsg, g_free);
#pragma clang diagnostic pop

FuLogitechHidppHidppMsg *
fu_logitech_hidpp_msg_new(void);
void
fu_logitech_hidpp_msg_copy(FuLogitechHidppHidppMsg *msg_dst,
			   const FuLogitechHidppHidppMsg *msg_src);
gsize
fu_logitech_hidpp_msg_get_payload_length(FuLogitechHidppHidppMsg *msg);
gboolean
fu_logitech_hidpp_msg_is_reply(FuLogitechHidppHidppMsg *msg1, FuLogitechHidppHidppMsg *msg2);
gboolean
fu_logitech_hidpp_msg_is_hidpp10_compat(FuLogitechHidppHidppMsg *msg);
gboolean
fu_logitech_hidpp_msg_is_error(FuLogitechHidppHidppMsg *msg, GError **error);
gboolean
fu_logitech_hidpp_msg_verify_swid(FuLogitechHidppHidppMsg *msg);

const gchar *
fu_logitech_hidpp_msg_fcn_id_to_string(FuLogitechHidppHidppMsg *msg);
