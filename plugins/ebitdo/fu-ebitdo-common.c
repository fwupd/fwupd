/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-ebitdo-common.h"

const gchar *
fu_ebitdo_pkt_type_to_string (FuEbitdoPktType cmd)
{
	if (cmd == FU_EBITDO_PKT_TYPE_USER_CMD)
		return "user-cmd";
	if (cmd == FU_EBITDO_PKT_TYPE_USER_DATA)
		return "user-data";
	if (cmd == FU_EBITDO_PKT_TYPE_MID_CMD)
		return "mid-cmd";
	return NULL;
}

const gchar *
fu_ebitdo_pkt_cmd_to_string (FuEbitdoPktCmd cmd)
{
	if (cmd == FU_EBITDO_PKT_CMD_FW_UPDATE_DATA)
		return "fw-update-data";
	if (cmd == FU_EBITDO_PKT_CMD_FW_UPDATE_HEADER)
		return "fw-update-header";
	if (cmd == FU_EBITDO_PKT_CMD_FW_UPDATE_OK)
		return "fw-update-ok";
	if (cmd == FU_EBITDO_PKT_CMD_FW_UPDATE_ERROR)
		return "fw-update-error";
	if (cmd == FU_EBITDO_PKT_CMD_FW_GET_VERSION)
		return "fw-get-version";
	if (cmd == FU_EBITDO_PKT_CMD_FW_SET_VERSION)
		return "fw-set-version";
	if (cmd == FU_EBITDO_PKT_CMD_FW_SET_ENCODE_ID)
		return "fw-set-encode-id";
	if (cmd == FU_EBITDO_PKT_CMD_ACK)
		return "ack";
	if (cmd == FU_EBITDO_PKT_CMD_NAK)
		return "nak";
	if (cmd == FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA)
		return "update-firmware-data";
	if (cmd == FU_EBITDO_PKT_CMD_TRANSFER_ABORT)
		return "transfer-abort";
	if (cmd == FU_EBITDO_PKT_CMD_VERIFICATION_ID)
		return "verification-id";
	if (cmd == FU_EBITDO_PKT_CMD_GET_VERIFICATION_ID)
		return "get-verification-id";
	if (cmd == FU_EBITDO_PKT_CMD_VERIFY_ERROR)
		return "verify-error";
	if (cmd == FU_EBITDO_PKT_CMD_VERIFY_OK)
		return "verify-ok";
	if (cmd == FU_EBITDO_PKT_CMD_TRANSFER_TIMEOUT)
		return "transfer-timeout";
	if (cmd == FU_EBITDO_PKT_CMD_GET_VERSION)
		return "get-version";
	if (cmd == FU_EBITDO_PKT_CMD_GET_VERSION_RESPONSE)
		return "get-version-response";
	return NULL;
}

void
fu_ebitdo_dump_pkt (FuEbitdoPkt *hdr)
{
	g_print ("PktLength:   0x%02x\n", hdr->pkt_len);
	g_print ("PktType:     0x%02x [%s]\n",
		 hdr->type, fu_ebitdo_pkt_type_to_string (hdr->type));
	g_print ("CmdSubtype:  0x%02x [%s]\n",
		 hdr->subtype, fu_ebitdo_pkt_cmd_to_string (hdr->subtype));
	g_print ("CmdLen:      0x%04x\n", GUINT16_FROM_LE (hdr->cmd_len));
	g_print ("Cmd:         0x%02x [%s]\n",
		 hdr->cmd, fu_ebitdo_pkt_cmd_to_string (hdr->cmd));
	g_print ("Payload Len: 0x%04x\n",
		 GUINT16_FROM_LE (hdr->payload_len));
}
