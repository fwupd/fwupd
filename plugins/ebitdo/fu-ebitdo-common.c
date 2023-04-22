/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ebitdo-common.h"
#include "fu-ebitdo-struct.h"

void
fu_ebitdo_dump_pkt(FuEbitdoPkt *hdr)
{
	g_print("PktLength:   0x%02x\n", hdr->pkt_len);
	g_print("PktType:     0x%02x [%s]\n", hdr->type, fu_ebitdo_pkt_type_to_string(hdr->type));
	g_print("CmdSubtype:  0x%02x [%s]\n",
		hdr->subtype,
		fu_ebitdo_pkt_cmd_to_string(hdr->subtype));
	g_print("CmdLen:      0x%04x\n", GUINT16_FROM_LE(hdr->cmd_len));
	g_print("Cmd:         0x%02x [%s]\n", hdr->cmd, fu_ebitdo_pkt_cmd_to_string(hdr->cmd));
	g_print("Payload Len: 0x%04x\n", GUINT16_FROM_LE(hdr->payload_len));
}
