/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 20157 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>

#include "dfu-chunked.h"

/**
 * dfu_chunked_packet_new:
 * @idx: the packet number
 * @page: the hardware memory page
 * @address: the address *within* the page
 * @data: the data
 * @data_sz: size of @data_sz
 *
 * Creates a new packet of chunked data.
 *
 * Return value: (transfer full): a #DfuChunkedPacket
 **/
DfuChunkedPacket *
dfu_chunked_packet_new (guint32 idx,
			guint32 page,
			guint32 address,
			const guint8 *data,
			guint32 data_sz)
{
	DfuChunkedPacket *item = g_new0 (DfuChunkedPacket, 1);
	item->idx = idx;
	item->page = page;
	item->address = address;
	item->data = data;
	item->data_sz = data_sz;
	return item;
}

/**
 * dfu_chunked_packet_to_string:
 * @item: a #DfuChunkedPacket
 *
 * Converts the chunked packet to a string representation.
 *
 * Return value: (transfer full): A string
 **/
gchar *
dfu_chunked_packet_to_string (DfuChunkedPacket *item)
{
	g_autoptr(GString) str = g_string_new (NULL);
	if (item->data != NULL) {
		for (guint32 i = 0; i < item->data_sz; i++) {
			gchar tmp = (gchar) item->data[i];
			if (tmp == 0x00)
				break;
			g_string_append_c (str, g_ascii_isalnum (tmp) ? tmp : '?');
		}
	}
	return g_strdup_printf ("#%02" G_GUINT32_FORMAT ": page:%02x "
				"addr:%04x len:%02" G_GUINT32_FORMAT " %s",
				item->idx,
				(guint) item->page,
				(guint) item->address,
				item->data_sz,
				str->str);
}

/**
 * dfu_chunked_to_string:
 * @segments: (element-type DfuChunkedPacket): array of packets
 *
 * Converts all the chunked packets in an array to a string representation.
 *
 * Return value: (transfer full): A string
 **/
gchar *
dfu_chunked_to_string (GPtrArray *segments)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < segments->len; i++) {
		DfuChunkedPacket *item = g_ptr_array_index (segments, i);
		g_autofree gchar *tmp = dfu_chunked_packet_to_string (item);
		g_string_append_printf (str, "%s\n", tmp);
	}
	return g_string_free (str, FALSE);
}

/**
 * dfu_chunked_new:
 * @data: a linear blob of memory, or %NULL
 * @data_sz: size of @data_sz
 * @addr_start: the hardware address offset, or 0
 * @page_sz: the hardware page size, or 0
 * @packet_sz: the transfer size, or 0
 *
 * Chunks a linear blob of memory into packets, ensuring each packet does not
 * cross a package boundary and is less that a specific transfer size.
 *
 * Return value: (element-type DfuChunkedPacket): array of packets
 **/
GPtrArray *
dfu_chunked_new (const guint8 *data,
		 guint32 data_sz,
		 guint32 addr_start,
		 guint32 page_sz,
		 guint32 packet_sz)
{
	GPtrArray *segments = NULL;
	guint32 page_old = G_MAXUINT32;
	guint32 idx;
	guint32 last_flush = 0;

	g_return_val_if_fail (data_sz > 0, NULL);

	segments = g_ptr_array_new_with_free_func (g_free);
	for (idx = 1; idx < data_sz; idx++) {
		guint32 page = 0;
		if (page_sz > 0)
			page = (addr_start + idx) / page_sz;
		if (page_old == G_MAXUINT32) {
			page_old = page;
		} else if (page != page_old) {
			const guint8 *data_offset = data != NULL ? data + last_flush : 0x0;
			guint32 address_offset = addr_start + last_flush;
			if (page_sz > 0)
				address_offset %= page_sz;
			g_ptr_array_add (segments,
					 dfu_chunked_packet_new (segments->len,
								 page_old,
								 address_offset,
								 data_offset,
								 idx - last_flush));
			last_flush = idx;
			page_old = page;
			continue;
		}
		if (packet_sz > 0 && idx - last_flush >= packet_sz) {
			const guint8 *data_offset = data != NULL ? data + last_flush : 0x0;
			guint32 address_offset = addr_start + last_flush;
			if (page_sz > 0)
				address_offset %= page_sz;
			g_ptr_array_add (segments,
					 dfu_chunked_packet_new (segments->len,
								 page,
								 address_offset,
								 data_offset,
								 idx - last_flush));
			last_flush = idx;
			continue;
		}
	}
	if (last_flush != idx) {
		const guint8 *data_offset = data != NULL ? data + last_flush : 0x0;
		guint32 address_offset = addr_start + last_flush;
		guint32 page = 0;
		if (page_sz > 0) {
			address_offset %= page_sz;
			page = (addr_start + (idx - 1)) / page_sz;
		}
		g_ptr_array_add (segments,
				 dfu_chunked_packet_new (segments->len,
							 page,
							 address_offset,
							 data_offset,
							 data_sz - last_flush));
	}
	return segments;
}
