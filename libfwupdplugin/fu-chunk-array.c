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
 */

struct _FuChunkArray {
	GObject parent_instance;
	GBytes *blob;
	GInputStream *stream;
	gsize addr_offset;
	gsize page_sz;
	gsize packet_sz;
	GArray *offsets; /* of gsize */
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
	return self->offsets->len;
}

static void
fu_chunk_array_calculate_chunk_for_offset(FuChunkArray *self,
					  gsize offset,
					  gsize *address,
					  gsize *page,
					  gsize *chunksz)
{
	gsize chunksz_tmp = MIN(self->packet_sz, self->total_size - offset);
	gsize page_tmp = 0;
	gsize address_tmp = self->addr_offset + offset;

	/* if page_sz is not specified then all the pages are 0 */
	if (self->page_sz > 0) {
		address_tmp %= self->page_sz;
		page_tmp = (offset + self->addr_offset) / self->page_sz;
	}

	/* cut the packet so it does not straddle multiple blocks */
	if (self->page_sz != self->packet_sz && self->page_sz > 0)
		chunksz_tmp = MIN(chunksz_tmp, (offset + self->packet_sz) % self->page_sz);

	/* all optional */
	if (address != NULL)
		*address = address_tmp;
	if (page != NULL)
		*page = page_tmp;
	if (chunksz != NULL)
		*chunksz = chunksz_tmp;
}

/**
 * fu_chunk_array_index:
 * @self: a #FuChunkArray
 * @idx: the chunk index
 * @error: (nullable): optional return location for an error
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
	gsize address = 0;
	gsize chunksz = 0;
	gsize offset;
	gsize page = 0;
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(GBytes) blob_chk = NULL;

	g_return_val_if_fail(FU_IS_CHUNK_ARRAY(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (idx >= self->offsets->len) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "idx %u invalid", idx);
		return NULL;
	}

	/* calculate address, page and chunk size from the offset */
	offset = g_array_index(self->offsets, gsize, idx);
	fu_chunk_array_calculate_chunk_for_offset(self, offset, &address, &page, &chunksz);
	if (chunksz == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "idx %u zero sized", idx);
		return NULL;
	}

	/* create new chunk */
	if (self->blob != NULL) {
		blob_chk = g_bytes_new_from_bytes(self->blob, offset, chunksz);
	} else if (self->stream != NULL) {
		blob_chk = fu_input_stream_read_bytes(self->stream, offset, chunksz, NULL, error);
		if (blob_chk == NULL) {
			g_prefix_error(error,
				       "failed to get stream at 0x%x for 0x%x: ",
				       (guint)offset,
				       (guint)chunksz);
			return NULL;
		}
	} else {
		blob_chk = g_bytes_new(NULL, 0);
	}
	chk = fu_chunk_bytes_new(blob_chk);
	fu_chunk_set_idx(chk, idx);
	fu_chunk_set_page(chk, page);
	fu_chunk_set_address(chk, address);
	return g_steal_pointer(&chk);
}

static void
fu_chunk_array_ensure_offsets(FuChunkArray *self)
{
	gsize offset = 0;
	while (offset < self->total_size) {
		gsize chunksz = 0;
		fu_chunk_array_calculate_chunk_for_offset(self, offset, NULL, NULL, &chunksz);
		g_array_append_val(self->offsets, offset);
		offset += chunksz;
	}
}

/**
 * fu_chunk_array_new_from_bytes:
 * @blob: data
 * @addr_offset: the hardware address offset, or %FU_CHUNK_ADDR_OFFSET_NONE
 * @page_sz: the hardware page size, typically %FU_CHUNK_PAGESZ_NONE
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
fu_chunk_array_new_from_bytes(GBytes *blob, gsize addr_offset, gsize page_sz, gsize packet_sz)
{
	g_autoptr(FuChunkArray) self = g_object_new(FU_TYPE_CHUNK_ARRAY, NULL);

	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(page_sz == 0 || page_sz >= packet_sz, NULL);

	self->addr_offset = addr_offset;
	self->page_sz = page_sz;
	self->packet_sz = packet_sz;
	self->blob = g_bytes_ref(blob);
	self->total_size = g_bytes_get_size(self->blob);

	/* success */
	fu_chunk_array_ensure_offsets(self);
	return g_steal_pointer(&self);
}

/**
 * fu_chunk_array_new_from_stream:
 * @stream: a #GInputStream
 * @addr_offset: the hardware address offset, or %FU_CHUNK_ADDR_OFFSET_NONE
 * @page_sz: the hardware page size, typically %FU_CHUNK_PAGESZ_NONE
 * @packet_sz: the packet size, or 0x0
 * @error: (nullable): optional return location for an error
 *
 * Chunks a linear stream into packets, ensuring each packet is less that a specific
 * transfer size.
 *
 * Returns: (transfer full): a #FuChunkArray, or #NULL on error
 *
 * Since: 2.0.2
 **/
FuChunkArray *
fu_chunk_array_new_from_stream(GInputStream *stream,
			       gsize addr_offset,
			       gsize page_sz,
			       gsize packet_sz,
			       GError **error)
{
	g_autoptr(FuChunkArray) self = g_object_new(FU_TYPE_CHUNK_ARRAY, NULL);

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	g_return_val_if_fail(page_sz == 0 || page_sz >= packet_sz, NULL);

	if (!fu_input_stream_size(stream, &self->total_size, error))
		return NULL;
	if (!g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_SET, NULL, error))
		return NULL;
	self->addr_offset = addr_offset;
	self->page_sz = page_sz;
	self->packet_sz = packet_sz;
	self->stream = g_object_ref(stream);

	/* success */
	fu_chunk_array_ensure_offsets(self);
	return g_steal_pointer(&self);
}

static void
fu_chunk_array_finalize(GObject *object)
{
	FuChunkArray *self = FU_CHUNK_ARRAY(object);
	g_array_unref(self->offsets);
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
	self->offsets = g_array_new(FALSE, FALSE, sizeof(gsize));
}
