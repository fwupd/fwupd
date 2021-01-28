/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuChunk"

#include "config.h"

#include <string.h>

#include "fu-chunk.h"

/**
 * SECTION:fu-chunk
 * @short_description: A packet of chunked data
 *
 * An object that represents a packet of data.
 *
 */

/**
 * fu_chunk_get_idx:
 * @self: a #FuChunk
 *
 * Gets the index of the chunk.
 *
 * Return value: index
 *
 * Since: 1.5.6
 **/
guint32
fu_chunk_get_idx (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, G_MAXUINT32);
	return self->idx;
}

/**
 * fu_chunk_get_page:
 * @self: a #FuChunk
 *
 * Gets the page of the chunk.
 *
 * Return value: page
 *
 * Since: 1.5.6
 **/
guint32
fu_chunk_get_page (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, G_MAXUINT32);
	return self->page;
}

/**
 * fu_chunk_get_address:
 * @self: a #FuChunk
 *
 * Gets the address of the chunk.
 *
 * Return value: address
 *
 * Since: 1.5.6
 **/
guint32
fu_chunk_get_address (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, G_MAXUINT32);
	return self->address;
}

/**
 * fu_chunk_get_data:
 * @self: a #FuChunk
 *
 * Gets the data of the chunk.
 *
 * Return value: bytes
 *
 * Since: 1.5.6
 **/
const guint8 *
fu_chunk_get_data (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->data;
}

/**
 * fu_chunk_get_data_out:
 * @self: a #FuChunk
 *
 * Gets the mutable data of the chunk.
 *
 * WARNING: At the moment fu_chunk_get_data_out() returns the same data as
 * fu_chunk_get_data() in all cases. The caller should verify the data passed to
 * fu_chunk_array_new() is also writable (i.e. not `const` or `mmap`) before
 * using this function.
 *
 * Return value: (transfer none): bytes
 *
 * Since: 1.5.6
 **/
guint8 *
fu_chunk_get_data_out (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (guint8 *) self->data;
}

/**
 * fu_chunk_get_data_sz:
 * @self: a #FuChunk
 *
 * Gets the data size of the chunk.
 *
 * Return value: size in bytes
 *
 * Since: 1.5.6
 **/
guint32
fu_chunk_get_data_sz (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, G_MAXUINT32);
	return self->data_sz;
}

/**
 * fu_chunk_get_bytes:
 * @self: a #FuChunk
 *
 * Gets the data as bytes of the chunk.
 *
 * Return value: (transfer full): a #GBytes
 *
 * Since: 1.5.6
 **/
GBytes *
fu_chunk_get_bytes (FuChunk *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return g_bytes_new_static (self->data, self->data_sz);
}

/**
 * fu_chunk_new: (skip):
 * @idx: the packet number
 * @page: the hardware memory page
 * @address: the address *within* the page
 * @data: the data
 * @data_sz: size of @data_sz
 *
 * Creates a new packet of chunked data.
 *
 * Return value: (transfer full): a #FuChunk
 *
 * Since: 1.1.2
 **/
FuChunk *
fu_chunk_new (guint32 idx,
	      guint32 page,
	      guint32 address,
	      const guint8 *data,
	      guint32 data_sz)
{
	FuChunk *item = g_new0 (FuChunk, 1);
	item->idx = idx;
	item->page = page;
	item->address = address;
	item->data = data;
	item->data_sz = data_sz;
	return item;
}

/**
 * fu_chunk_to_string:
 * @item: a #FuChunk
 *
 * Converts the chunked packet to a string representation.
 *
 * Return value: (transfer full): A string
 *
 * Since: 1.1.2
 **/
gchar *
fu_chunk_to_string (FuChunk *item)
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
 * fu_chunk_array_to_string:
 * @chunks: (element-type FuChunk): array of packets
 *
 * Converts all the chunked packets in an array to a string representation.
 *
 * Return value: (transfer full): A string
 *
 * Since: 1.0.1
 **/
gchar *
fu_chunk_array_to_string (GPtrArray *chunks)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *item = g_ptr_array_index (chunks, i);
		g_autofree gchar *tmp = fu_chunk_to_string (item);
		g_string_append_printf (str, "%s\n", tmp);
	}
	return g_string_free (str, FALSE);
}

/**
 * fu_chunk_array_new: (skip):
 * @data: a linear blob of memory, or %NULL
 * @data_sz: size of @data_sz
 * @addr_start: the hardware address offset, or 0
 * @page_sz: the hardware page size, or 0
 * @packet_sz: the transfer size, or 0
 *
 * Chunks a linear blob of memory into packets, ensuring each packet does not
 * cross a package boundary and is less that a specific transfer size.
 *
 * Return value: (transfer container) (element-type FuChunk): array of packets
 *
 * Since: 1.1.2
 **/
GPtrArray *
fu_chunk_array_new (const guint8 *data,
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
					 fu_chunk_new (segments->len,
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
					 fu_chunk_new (segments->len,
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
				 fu_chunk_new (segments->len,
					       page,
					       address_offset,
					       data_offset,
					       data_sz - last_flush));
	}
	return segments;
}

/**
 * fu_chunk_array_new_from_bytes: (skip):
 * @blob: a #GBytes
 * @addr_start: the hardware address offset, or 0
 * @page_sz: the hardware page size, or 0
 * @packet_sz: the transfer size, or 0
 *
 * Chunks a linear blob of memory into packets, ensuring each packet does not
 * cross a package boundary and is less that a specific transfer size.
 *
 * Return value: (transfer container) (element-type FuChunk): array of packets
 *
 * Since: 1.1.2
 **/
GPtrArray *
fu_chunk_array_new_from_bytes (GBytes *blob,
			       guint32 addr_start,
			       guint32 page_sz,
			       guint32 packet_sz)
{
	gsize sz;
	const guint8 *data = g_bytes_get_data (blob, &sz);
	return fu_chunk_array_new (data, (guint32) sz,
				   addr_start, page_sz, packet_sz);
}
