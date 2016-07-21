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

#include <glib.h>
#include <string.h>

#include "ebitdo-common.h"

//Level,Sp,Index,m:s.ms.us,Dur,Len,Err,Dev,Ep,Record,Summary

typedef enum {
	CSV_COLUMN_LEVEL,
	CSV_COLUMN_SP,
	CSV_COLUMN_INDEX,
	CSV_COLUMN_TIMESTAMP,
	CSV_COLUMN_DURATION,
	CSV_COLUMN_LENGTH,
	CSV_COLUMN_ERROR,
	CSV_COLUMN_DEVICE,
	CSV_COLUMN_ENDPOINT,
	CSV_COLUMN_RECORD,
	CSV_COLUMN_SUMMARY,
	CSV_COLUMN_LAST
} CsvColumn;


void
ebitdo_dump_pkt_small (EbitdoPkt *hdr)
{
	g_print ("CmdSubtype:  0x%02x [%s]\n",
		 hdr->subtype, ebitdo_pkt_subtype_to_string (hdr->subtype));
	g_print ("Cmd:         0x%02x [%s]\n",
		 hdr->cmd, ebitdo_pkt_cmd_to_string (hdr->cmd));
	g_print ("Payload Len: 0x%04x\n",
		 GUINT16_FROM_LE (hdr->payload_len));
}

static void
ebitdo_process_csv_line (const gchar *line)
{
	g_auto(GStrv) data = NULL;
	guint i;
	guint8 buffer[64];
	const gchar *title;
	EbitdoPkt *hdr = (EbitdoPkt *) buffer;

	/* comment */
	if (line[0] == '#')
		return;
	if (line[0] == '\0')
		return;

	/* process data */
	data = g_strsplit (line, ",", -1);
	if (g_strv_length (data) < CSV_COLUMN_LAST)
		return;

	/* only interested in 64 byte transfers */
	if (g_strcmp0 (data[CSV_COLUMN_LENGTH], "64 B") != 0)
		return;

	/* only interested in EP1 and EP2 */
	if (g_strcmp0 (data[CSV_COLUMN_ENDPOINT], "01") == 0) {
		title = "Request";
	} else if (g_strcmp0 (data[CSV_COLUMN_ENDPOINT], "02") == 0) {
		title = "Response";
	} else {
		return;
	}

	/* parse data */
	memset (buffer, 0x00, sizeof(buffer));
	for (i = 0; i < 64; i++) {
		guint64 tmp;
		if (i * 3 > strlen (data[CSV_COLUMN_SUMMARY]))
			break;
		tmp = g_ascii_strtoull (data[CSV_COLUMN_SUMMARY] + (i * 3),
					NULL, 16);
		if (tmp > 0xff)
			break;
		buffer[i] = tmp;
	}

	/* filter out transfer timeouts */
	if (0) {
		if (hdr->type == EBITDO_PKT_TYPE_USER_CMD &&
		    hdr->subtype == EBITDO_PKT_SUBTYPE_TRANSFER_TIMEOUT)
			return;
	}

	ebitdo_dump_raw (title, buffer, hdr->pkt_len);
	ebitdo_dump_pkt_small (hdr);
	g_print ("\n");
}

int
main (int argc, char **argv)
{
	g_autofree gchar *data = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GError) error = NULL;
	guint i;

	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* require filename */
	if (argc != 2) {
		g_print ("USAGE: %s <filename>\n", argv[0]);
		return 1;
	}

	/* get data */
	if (!g_file_get_contents (argv[1], &data, NULL, &error)) {
		g_print ("%s\n", error->message);
		return 1;
	}

	lines = g_strsplit (data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		ebitdo_process_csv_line (lines[i]);
	}

	return 0;
}
