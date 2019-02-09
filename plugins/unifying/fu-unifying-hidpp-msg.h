/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	FU_UNIFYING_HIDPP_MSG_FLAG_NONE,
	FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT	= 1 << 0,
	FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID	= 1 << 1,
	FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID	= 1 << 2,
	FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SWID		= 1 << 3,
	/*< private >*/
	FU_UNIFYING_HIDPP_MSG_FLAG_LAST
} FuUnifyingHidppMsgFlags;

typedef struct __attribute__((packed)) {
	guint8	 report_id;
	guint8	 device_id;
	guint8	 sub_id;
	guint8	 function_id;	/* funcId:software_id */
	guint8	 data[47];	/* maximum supported by Windows XP SP2 */
	/* not included in the packet sent to the hardware */
	guint32	 flags;
	guint8	 hidpp_version;
} FuUnifyingHidppMsg;

/* this is specific to fwupd */
#define FU_UNIFYING_HIDPP_MSG_SW_ID		0x07

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUnifyingHidppMsg, g_free);
#pragma clang diagnostic pop

FuUnifyingHidppMsg *fu_unifying_hidpp_msg_new			(void);
void		 fu_unifying_hidpp_msg_copy			(FuUnifyingHidppMsg	*msg_dst,
								 const FuUnifyingHidppMsg *msg_src);
gsize		 fu_unifying_hidpp_msg_get_payload_length	(FuUnifyingHidppMsg	*msg);
gboolean	 fu_unifying_hidpp_msg_is_reply			(FuUnifyingHidppMsg	*msg1,
								 FuUnifyingHidppMsg	*msg2);
gboolean	 fu_unifying_hidpp_msg_is_hidpp10_compat	(FuUnifyingHidppMsg	*msg);
gboolean	 fu_unifying_hidpp_msg_is_error			(FuUnifyingHidppMsg	*msg,
								 GError			**error);
gboolean	 fu_unifying_hidpp_msg_verify_swid		(FuUnifyingHidppMsg	*msg);

const gchar	*fu_unifying_hidpp_msg_dev_id_to_string		(FuUnifyingHidppMsg	*msg);
const gchar	*fu_unifying_hidpp_msg_rpt_id_to_string		(FuUnifyingHidppMsg	*msg);
const gchar	*fu_unifying_hidpp_msg_sub_id_to_string		(FuUnifyingHidppMsg	*msg);
const gchar	*fu_unifying_hidpp_msg_fcn_id_to_string		(FuUnifyingHidppMsg	*msg);

G_END_DECLS
