/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_HIDPP_MSG_H
#define __LU_HIDPP_MSG_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	LU_HIDPP_MSG_FLAG_NONE,
	LU_HIDPP_MSG_FLAG_LONGER_TIMEOUT	= 1 << 0,
	LU_HIDPP_MSG_FLAG_IGNORE_SUB_ID		= 1 << 1,
	LU_HIDPP_MSG_FLAG_IGNORE_FNCT_ID	= 1 << 2,
	LU_HIDPP_MSG_FLAG_IGNORE_SWID		= 1 << 3,
	/*< private >*/
	LU_HIDPP_MSG_FLAG_LAST
} LuHidppMsgFlags;

typedef struct __attribute__((packed)) {
	guint8	 report_id;
	guint8	 device_id;
	guint8	 sub_id;
	guint8	 function_id;	/* funcId:software_id */
	guint8	 data[47];	/* maximum supported by Windows XP SP2 */
	/* not included in the packet sent to the hardware */
	guint32	 flags;
} LuHidppMsg;

/* this is specific to fwupd */
#define LU_HIDPP_MSG_SW_ID		0x07

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(LuHidppMsg, g_free);
#pragma clang diagnostic pop

LuHidppMsg	*lu_hidpp_msg_new			(void);
void		 lu_hidpp_msg_copy			(LuHidppMsg	*msg_dst,
							 LuHidppMsg	*msg_src);
gsize		 lu_hidpp_msg_get_payload_length	(LuHidppMsg	*msg);
gboolean	 lu_hidpp_msg_is_reply			(LuHidppMsg	*msg1,
							 LuHidppMsg	*msg2);
gboolean	 lu_hidpp_msg_is_hidpp10_compat		(LuHidppMsg	*msg);
gboolean	 lu_hidpp_msg_is_error			(LuHidppMsg	*msg,
							 GError		**error);
gboolean	 lu_hidpp_msg_verify_swid		(LuHidppMsg	*msg);

const gchar	*lu_hidpp_msg_dev_id_to_string		(LuHidppMsg	*msg);
const gchar	*lu_hidpp_msg_rpt_id_to_string		(LuHidppMsg	*msg);
const gchar	*lu_hidpp_msg_sub_id_to_string		(LuHidppMsg	*msg);
const gchar	*lu_hidpp_msg_fcn_id_to_string		(LuHidppMsg	*msg);

G_END_DECLS

#endif /* __LU_HIDPP_MSG_H */
