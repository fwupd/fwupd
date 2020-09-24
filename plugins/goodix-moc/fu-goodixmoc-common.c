/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "fu-goodixmoc-common.h"
#include "fu-common.h"

void 
fu_goodixmoc_build_header (GxfpPkgHeader *pheader,
			   guint16	  len,
			   guint8	  cmd0,
			   guint8	  cmd1,
		  	   GxPkgType	  type)
{
	static guint8 dummy_seq = 0;
	g_assert (pheader);
	memset (pheader, 0, sizeof(*pheader));
	pheader->cmd0 = (cmd0);
	pheader->cmd1 = (cmd1);
	pheader->pkg_flag = (guint8)type;
	pheader->reserved = dummy_seq++;
	pheader->len = len + GX_SIZE_CRC32;
	pheader->crc8 = fu_common_crc8 ((guint8 *)pheader, 6);
	pheader->rev_crc8 = ~pheader->crc8;
}

gboolean
fu_goodixmoc_parse_header (guint8 *buf, guint32 bufsz, GxfpPkgHeader *pheader)
{
	if (!buf || !pheader)
		return FALSE;
	if (bufsz < sizeof(*pheader))
		return FALSE;
	memcpy (pheader, buf, sizeof(*pheader));
	pheader->len = GUINT16_FROM_LE(*(buf + 4));
	pheader->len -= GX_SIZE_CRC32;
	return TRUE;
}

gboolean
fu_goodixmoc_parse_body (guint8 cmd, guint8 *buf, guint32 bufsz, GxfpCmdResp *presp)
{
	g_assert (buf != NULL);
	g_assert (presp != NULL);
	g_assert (bufsz >= 1);
	presp->result = buf[0];
	switch (cmd) {
	case GX_CMD_ACK:
		presp->ack_msg.cmd = buf[1];
		break;
	case GX_CMD_VERSION:
		memcpy (&presp->version_info, buf + 1, sizeof(GxfpVersiomInfo));
		break;
	default:
		break;
	}
	return TRUE;
}
