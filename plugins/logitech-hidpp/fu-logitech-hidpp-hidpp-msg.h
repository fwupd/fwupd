/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef enum {
	FU_UNIFYING_HIDPP_MSG_FLAG_NONE,
	FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT	= 1 << 0,
	FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID	= 1 << 1,
	FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID	= 1 << 2,
	FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SWID		= 1 << 3,
	/*< private >*/
	FU_UNIFYING_HIDPP_MSG_FLAG_LAST
} FuLogitechHidPpHidppMsgFlags;

typedef struct __attribute__((packed)) {
	guint8	 report_id;
	guint8	 device_id;
	guint8	 sub_id;
	guint8	 function_id;	/* funcId:software_id */
	guint8	 data[47];	/* maximum supported by Windows XP SP2 */
	/* not included in the packet sent to the hardware */
	guint32	 flags;
	guint8	 hidpp_version;
} FuLogitechHidPpHidppMsg;

/* this is specific to fwupd */
#define FU_UNIFYING_HIDPP_MSG_SW_ID		0x07

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuLogitechHidPpHidppMsg, g_free);
#pragma clang diagnostic pop

FuLogitechHidPpHidppMsg *fu_logitech_hidpp_msg_new			(void);
void		 fu_logitech_hidpp_msg_copy			(FuLogitechHidPpHidppMsg	*msg_dst,
								 const FuLogitechHidPpHidppMsg *msg_src);
gsize		 fu_logitech_hidpp_msg_get_payload_length	(FuLogitechHidPpHidppMsg	*msg);
gboolean	 fu_logitech_hidpp_msg_is_reply			(FuLogitechHidPpHidppMsg	*msg1,
								 FuLogitechHidPpHidppMsg	*msg2);
gboolean	 fu_logitech_hidpp_msg_is_hidpp10_compat	(FuLogitechHidPpHidppMsg	*msg);
gboolean	 fu_logitech_hidpp_msg_is_error			(FuLogitechHidPpHidppMsg	*msg,
								 GError				**error);
gboolean	 fu_logitech_hidpp_msg_verify_swid		(FuLogitechHidPpHidppMsg	*msg);

const gchar	*fu_logitech_hidpp_msg_dev_id_to_string		(FuLogitechHidPpHidppMsg	*msg);
const gchar	*fu_logitech_hidpp_msg_rpt_id_to_string		(FuLogitechHidPpHidppMsg	*msg);
const gchar	*fu_logitech_hidpp_msg_sub_id_to_string		(FuLogitechHidPpHidppMsg	*msg);
const gchar	*fu_logitech_hidpp_msg_fcn_id_to_string		(FuLogitechHidPpHidppMsg	*msg);
