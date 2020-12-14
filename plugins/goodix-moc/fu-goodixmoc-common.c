/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <string.h>

#include "fu-common.h"
#include "fu-goodixmoc-common.h"

void
fu_goodixmoc_build_header (GxfpPkgHeader *pheader,
			   guint16	  len,
			   guint8	  cmd0,
			   guint8	  cmd1,
			   GxPkgType	  type)
{
	static guint8 dummy_seq = 0;

	g_return_if_fail (pheader != NULL);

	pheader->cmd0 = (cmd0);
	pheader->cmd1 = (cmd1);
	pheader->pkg_flag = (guint8)type;
	pheader->reserved = dummy_seq++;
	pheader->len = len + GX_SIZE_CRC32;
	pheader->crc8 = fu_common_crc8 ((guint8 *)pheader, 6);
	pheader->rev_crc8 = ~pheader->crc8;
}

gboolean
fu_goodixmoc_parse_header (guint8 *buf, guint32 bufsz,
			   GxfpPkgHeader *pheader, GError **error)
{
	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (pheader != NULL, FALSE);

	if (!fu_memcpy_safe ((guint8 *) &pheader, sizeof(*pheader), 0x0,	/* dst */
			     buf, bufsz, 0x01,					/* src */
			     sizeof(*pheader), error))
		return FALSE;
	memcpy (pheader, buf, sizeof(*pheader));
	pheader->len = GUINT16_FROM_LE(*(buf + 4));
	pheader->len -= GX_SIZE_CRC32;
	return TRUE;
}

gboolean
fu_goodixmoc_parse_body (guint8 cmd, guint8 *buf, guint32 bufsz,
			 GxfpCmdResp *presp, GError **error)
{
	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (presp != NULL, FALSE);

	presp->result = buf[0];
	switch (cmd) {
	case GX_CMD_ACK:
		if (bufsz == 0) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "invalid bufsz");
			return FALSE;
		}
		presp->ack_msg.cmd = buf[1];
		break;
	case GX_CMD_VERSION:
		if (!fu_memcpy_safe ((guint8 *) &presp->version_info,
				     sizeof(presp->version_info), 0x0,		/* dst */
				     buf, bufsz, 0x01,				/* src */
				     sizeof(GxfpVersiomInfo), error))
			return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}
