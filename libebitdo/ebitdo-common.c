/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "ebitdo-common.h"

const gchar *
ebitdo_pkt_type_to_string (EbitdoPktType cmd)
{
	if (cmd == EBITDO_PKT_TYPE_USER_CMD)
		return "user-cmd";
	if (cmd == EBITDO_PKT_TYPE_USER_DATA)
		return "user-data";
	if (cmd == EBITDO_PKT_TYPE_MID_CMD)
		return "mid-cmd";
	return NULL;
}

const gchar *
ebitdo_pkt_subtype_to_string (EbitdoPktSubtype cmd)
{
	if (cmd == EBITDO_PKT_SUBTYPE_ACK)
		return "ack";
	if (cmd == EBITDO_PKT_SUBTYPE_NAK)
		return "nak";
	if (cmd == EBITDO_PKT_SUBTYPE_UPDATE_FIRMWARE_DATA)
		return "update-firmware-data";
	if (cmd == EBITDO_PKT_SUBTYPE_TRANSFER_ABORT)
		return "transfer-abort";
	if (cmd == EBITDO_PKT_SUBTYPE_VERIFICATION_ID)
		return "verification-id";
	if (cmd == EBITDO_PKT_SUBTYPE_GET_VERIFICATION_ID)
		return "get-verification-id";
	if (cmd == EBITDO_PKT_SUBTYPE_VERIFY_ERROR)
		return "verify-error";
	if (cmd == EBITDO_PKT_SUBTYPE_VERIFY_OK)
		return "verify-ok";
	if (cmd == EBITDO_PKT_SUBTYPE_TRANSFER_TIMEOUT)
		return "transfer-timeout";
	if (cmd == EBITDO_PKT_SUBTYPE_GET_VERSION)
		return "get-version";
	if (cmd == EBITDO_PKT_SUBTYPE_GET_VERSION_RESPONSE)
		return "get-version-response";
	return NULL;
}

const gchar *
ebitdo_pkt_cmd_to_string (EbitdoPktCmd cmd)
{
	if (cmd == EBITDO_PKT_CMD_FW_UPDATE_DATA)
		return "fw-update-data";
	if (cmd == EBITDO_PKT_CMD_FW_UPDATE_HEADER)
		return "fw-update-header";
	if (cmd == EBITDO_PKT_CMD_FW_UPDATE_OK)
		return "fw-update-ok";
	if (cmd == EBITDO_PKT_CMD_FW_UPDATE_ERROR)
		return "fw-update-error";
	if (cmd == EBITDO_PKT_CMD_FW_GET_VERSION)
		return "fw-get-version";
	if (cmd == EBITDO_PKT_CMD_FW_SET_VERSION)
		return "fw-set-version";
	if (cmd == EBITDO_PKT_CMD_FW_SET_ENCODE_ID)
		return "fw-set-encode-id";
	return NULL;
}

void
ebitdo_dump_raw (const gchar *title, const guint8 *data, gsize len)
{
	guint i;

	g_print ("%s:", title);
	for (i = strlen (title); i < 16; i++)
		g_print (" ");
	for (i = 0; i < len; i++) {
		g_print ("%02x ", data[i]);
		if (i > 0 && i % 32 == 0)
			g_print ("\n");
	}
	g_print ("\n");
}

void
ebitdo_dump_pkt (EbitdoPkt *hdr)
{
	g_print ("PktLength:   0x%02x\n", hdr->pkt_len);
	g_print ("PktType:     0x%02x [%s]\n",
		 hdr->type, ebitdo_pkt_type_to_string (hdr->type));
	g_print ("CmdSubtype:  0x%02x [%s]\n",
		 hdr->subtype, ebitdo_pkt_subtype_to_string (hdr->subtype));
	g_print ("CmdLen:      0x%04x\n", GUINT16_FROM_LE (hdr->cmd_len));
	g_print ("Cmd:         0x%02x [%s]\n",
		 hdr->cmd, ebitdo_pkt_cmd_to_string (hdr->cmd));
	g_print ("Payload Len: 0x%04x\n",
		 GUINT16_FROM_LE (hdr->payload_len));
}

void
ebitdo_dump_firmware_header (EbitdoFirmwareHeader *hdr)
{
	g_print ("Version:             %.2f\n",
		 (gdouble) GUINT32_FROM_LE (hdr->version) / 100.f);
	g_print ("Destination Address: %x\n",
		 GUINT32_FROM_LE (hdr->destination_addr));
	g_print ("Destination Length:  %i\n",
		 GUINT32_FROM_LE (hdr->destination_len));
}
