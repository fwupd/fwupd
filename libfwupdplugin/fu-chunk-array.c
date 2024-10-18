/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuChunkArray"

#include "config.h"

#include "fu-bytes.h"
#include "fu-chunk-array.h"
#include "fu-input-stream.h"

/**
 * FuChunkArray:
 *
 * Create chunked data with address and index as required.
 *
 * NOTE: If you need a page size, either use fu_chunk_array_new() or use two #FuChunkArray's --
 * e.g. once to split to page size, and once to split to packet size.
 */

struct _FuChunkArray {
	GObject parent_instance;
	GBytes *blob;
	GInputStream *stream;
	guint32 addr_start;
	guint32 packet_sz;
	guint total_chunks;
	gsize total_size;
};

G_DEFINE_TYPE(FuChunkArray, fu_chunk_array, G_TYPE_OBJECT)

/**
 * fu_chunk_array_length:
 * @self: a #FuChunkArray
 *
 * Gets the number of chunks.
 *
 * Returns: integer
 *
 * Since: 1.9.6
 **/
guint
fu_chunk_array_length(FuChunkArray *self)
{
	g_return_val_if_fail(FU_IS_CHUNK_ARRAY(self), G_MAXUINT);
	return self->total_chunks;
}

/**
 * fu_chunk_array_index:
 * @self: a #FuChunkArray
 * @idx: the chunk index
 *
 * Gets the next chunk.
 *
 * Returns: (transfer full): a #FuChunk or %NULL if not valid
 *
 * Since: 1.9.6
 **/
FuChunk *
fu_chunk_array_index(FuChunkArray *self, guint idx, GError **error)
{
	gsize length;
	gsize offset;
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(GBytes) blob_chk = NULL;

	g_return_val_if_fail(FU_IS_CHUNK_ARRAY(self), NULL);

	/* calculate offset and length */
	offset = (gsize)idx * (gsize)self->packet_sz;
	if (offset >= self->total_size) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "idx %u invalid", idx);
		return NULL;
	}
	length = MIN(self->packet_sz, self->total_size - offset);
	if (length == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "idx %u zero sized", idx);
		return NULL;
	}

	/* create new chunk */
	if (self->blob != NULL) {
		blob_chk = g_bytes_new_from_bytes(self->blob, offset, length);
	} else if (self->stream != NULL) {
		blob_chk = fu_input_stream_read_bytes(self->stream, offset, length, NULL, error);
		if (blob_chk == NULL) {
			g_prefix_error(error,
				       "failed to get stream at 0x%x for 0x%x: ",
				       (guint)offset,
				       (guint)length);
			return NULL;
		}
	} else {
		blob_chk = g_bytes_new(NULL, 0);
	}
	chk = fu_chunk_bytes_new(blob_chk);
	fu_chunk_set_idx(chk, idx);
	fu_chunk_set_address(chk, self->addr_start + offset);
	return g_steal_pointer(&chk);
}

/**
 * fu_chunk_array_new_from_bytes:
 * @blob: data
 * @addr_start: the hardware address offset, or 0x0
 * @packet_sz: the packet size, or 0x0
 *
 * Chunks a linear blob of memory into packets, ensuring each packet is less that a specific
 * transfer size.
 *
 * Returns: (transfer full): a #FuChunkArray
 *
 * Since: 1.9.6
 **/
FuChunkArray *
fu_chunk_array_new_from_bytes(GBytes *blob, guint32 addr_start, guint32 packet_sz)
{
	g_autoptr(FuChunkArray) self = g_object_new(FU_TYPE_CHUNK_ARRAY, NULL);

	g_return_val_if_fail(blob != NULL, NULL);

	self->addr_start = addr_start;
	self->packet_sz = packet_sz;
	self->blob = g_bytes_ref(blob);
	self->total_size = g_bytes_get_size(self->blob);
	self->total_chunks = self->total_size / self->packet_sz;
	if (self->total_size % self->packet_sz != 0)
		self->total_chunks++;
	return g_steal_pointer(&self);
}

/**
 * fu_chunk_array_new_from_stream:
 * @stream: a #GInputStream
 * @addr_start: the hardware address offset, or 0x0
 * @packet_sz: the packet size, or 0x0
 * @error: (nullable): optional return location for an error
 *
 * Chunks a linear stream into packets, ensuring each packet is less that a specific
 * transfer size.
 *
 * Returns: (transfer full): a #FuChunkArray, or #NULL on error
 *
 * Since: 2.0.0
 **/
FuChunkArray *
fu_chunk_array_new_from_stream(GInputStream *stream,
			       guint32 addr_start,
			       guint32 packet_sz,
			       GError **error)
{
	g_autoptr(FuChunkArray) self = g_object_new(FU_TYPE_CHUNK_ARRAY, NULL);

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_input_stream_size(stream, &self->total_size, error))
		return NULL;
	if (!g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_SET, NULL, error))
		return NULL;
	self->addr_start = addr_start;
	self->packet_sz = packet_sz;
	self->stream = g_object_ref(stream);
	self->total_chunks = self->total_size / self->packet_sz;
	if (self->total_size % self->packet_sz != 0)
		self->total_chunks++;
	return g_steal_pointer(&self);
}

static void
fu_chunk_array_finalize(GObject *object)
{
	FuChunkArray *self = FU_CHUNK_ARRAY(object);
	if (self->blob != NULL)
		g_bytes_unref(self->blob);
	if (self->stream != NULL)
		g_object_unref(self->stream);
	G_OBJECT_CLASS(fu_chunk_array_parent_class)->finalize(object);
}

static void
fu_chunk_array_class_init(FuChunkArrayClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_chunk_array_finalize;
}

static void
fu_chunk_array_init(FuChunkArray *self)
{
}
