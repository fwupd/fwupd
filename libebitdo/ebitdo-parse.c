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
#include <gusb.h>
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

static void
ebitdo_dump_pkt_small (EbitdoPkt *hdr)
{
	g_print ("CmdSubtype:  0x%02x [%s]\n",
		 hdr->subtype, ebitdo_pkt_cmd_to_string (hdr->subtype));
	g_print ("Cmd:         0x%02x [%s]\n",
		 hdr->cmd, ebitdo_pkt_cmd_to_string (hdr->cmd));
	g_print ("Payload Len: 0x%04x\n",
		 GUINT16_FROM_LE (hdr->payload_len));
}

typedef struct {
	guint64			 ts;
	GUsbDeviceDirection	 direction;
	guint8			 buffer[64];
} EbitdoParseItem;

/* parse a timestamp like m:ss.ms.us, e.g. `0:09.643.335` */
static guint64
parse_timestamp (const gchar *data)
{
	guint64 smu[4];
	guint64 ts;
	guint i;
	g_auto(GStrv) ts_split = NULL;
	ts_split = g_strsplit_set (data, ":.", -1);
	for (i = 0; i < 4; i++)
		smu[i] = g_ascii_strtoull (ts_split[i], NULL, 10);
	//g_print ("%s,%s,%s,%s\n", ts_split[0], ts_split[1], ts_split[2], ts_split[3]);
	ts = smu[0] * G_USEC_PER_SEC * 60;
	ts += smu[1] * G_USEC_PER_SEC;
	ts += smu[2] * 1000;
	ts += smu[3];
	return ts;
}

static void
ebitdo_process_csv_line (GPtrArray *array, const gchar *line)
{
	g_auto(GStrv) data = NULL;
	guint i;
	guint8 buffer[64];
	gboolean direction = FALSE;
	EbitdoParseItem *item;

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
		direction = G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE;
	} else if (g_strcmp0 (data[CSV_COLUMN_ENDPOINT], "02") == 0) {
		direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST;
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

	/* add object */
	item = g_new0 (EbitdoParseItem, 1);
	item->direction = direction;
	item->ts = parse_timestamp (data[CSV_COLUMN_TIMESTAMP]);
	memcpy (item->buffer, buffer, 64);
	g_ptr_array_add (array, item);
}

static gint
ebitdo_sort_array_by_ts_cb (gconstpointer a, gconstpointer b)
{
	EbitdoParseItem *item1 = *((EbitdoParseItem **) a);
	EbitdoParseItem *item2 = *((EbitdoParseItem **) b);
	if (item1->ts < item2->ts)
		return -1;
	if (item2->ts < item1->ts)
		return 1;
	return 0;
}

int
main (int argc, char **argv)
{
	g_autofree gchar *data = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GError) error = NULL;
	guint i;
	guint64 ts_old = 0;
	g_autoptr(GPtrArray) array = NULL;

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
	array = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++)
		ebitdo_process_csv_line (array, lines[i]);
	g_ptr_array_sort (array, ebitdo_sort_array_by_ts_cb);
	for (i = 0; i < array->len; i++) {
		const gchar *title = "->DEVICE";
		EbitdoParseItem *item = g_ptr_array_index (array, i);
		EbitdoPkt *pkt = (EbitdoPkt *) item->buffer;
		if (ts_old > 0) {
			g_print ("wait %" G_GUINT64_FORMAT "ms\n\n",
				 (item->ts - ts_old) / 1000);
		}
		ts_old = item->ts;
		if (item->direction == G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST)
			title = "<- DEVICE";
		ebitdo_dump_raw (title, item->buffer, pkt->pkt_len + 1);
		ebitdo_dump_pkt_small (pkt);
		g_print ("\n");
	}

	return 0;
}
