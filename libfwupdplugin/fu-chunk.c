/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuChunk"

#include "config.h"

#include <string.h>

#include "fu-bytes.h"
#include "fu-chunk-private.h"
#include "fu-common.h"
#include "fu-input-stream.h"
#include "fu-mem.h"

/**
 * FuChunk:
 *
 * A optionally mutable packet of chunked data with address, page and index.
 */

struct _FuChunk {
	GObject parent_instance;
	guint idx;
	guint page;
	gsize address;
	guint8 *data_out; /* noref */
	gsize data_sz;
	GBytes *bytes;
	GInputStream *stream;
};

G_DEFINE_TYPE(FuChunk, fu_chunk, G_TYPE_OBJECT)

/**
 * fu_chunk_set_idx:
 * @self: a #FuChunk
 * @idx: index, starting at 0
 *
 * Sets the index of the chunk.
 *
 * Since: 1.5.6
 **/
void
fu_chunk_set_idx(FuChunk *self, guint idx)
{
	g_return_if_fail(FU_IS_CHUNK(self));
	self->idx = idx;
}

/**
 * fu_chunk_get_idx:
 * @self: a #FuChunk
 *
 * Gets the index of the chunk.
 *
 * Returns: index
 *
 * Since: 1.5.6
 **/
guint
fu_chunk_get_idx(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXUINT32);
	return self->idx;
}

/**
 * fu_chunk_set_page:
 * @self: a #FuChunk
 * @page: page number, starting at 0
 *
 * Sets the page of the chunk.
 *
 * Since: 1.5.6
 **/
void
fu_chunk_set_page(FuChunk *self, guint page)
{
	g_return_if_fail(FU_IS_CHUNK(self));
	self->page = page;
}

/**
 * fu_chunk_get_page:
 * @self: a #FuChunk
 *
 * Gets the page of the chunk.
 *
 * Returns: page
 *
 * Since: 1.5.6
 **/
guint
fu_chunk_get_page(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXUINT32);
	return self->page;
}

/**
 * fu_chunk_set_address:
 * @self: a #FuChunk
 * @address: memory address
 *
 * Sets the address of the chunk.
 *
 * Since: 1.5.6
 **/
void
fu_chunk_set_address(FuChunk *self, gsize address)
{
	g_return_if_fail(FU_IS_CHUNK(self));
	self->address = address;
}

/**
 * fu_chunk_get_address:
 * @self: a #FuChunk
 *
 * Gets the address of the chunk.
 *
 * Returns: address
 *
 * Since: 1.5.6
 **/
gsize
fu_chunk_get_address(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXUINT32);
	return self->address;
}

/**
 * fu_chunk_get_data_out:
 * @self: a #FuChunk
 *
 * Gets the mutable data of the chunk.
 *
 * Returns: (transfer none): bytes
 *
 * Since: 1.5.6
 **/
guint8 *
fu_chunk_get_data_out(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), NULL);
	return self->data_out;
}

/**
 * fu_chunk_get_data_sz:
 * @self: a #FuChunk
 *
 * Gets the data size of the chunk.
 *
 * Returns: size in bytes
 *
 * Since: 1.5.6
 **/
gsize
fu_chunk_get_data_sz(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXUINT32);
	return self->data_sz;
}

/* private */
void
fu_chunk_set_data_sz(FuChunk *self, gsize data_sz)
{
	g_return_if_fail(FU_IS_CHUNK(self));
	self->data_sz = data_sz;
}

/**
 * fu_chunk_set_bytes:
 * @self: a #FuChunk
 * @bytes: (nullable): data
 *
 * Sets the data to use for the chunk.
 *
 * Since: 1.5.6
 **/
void
fu_chunk_set_bytes(FuChunk *self, GBytes *bytes)
{
	g_return_if_fail(FU_IS_CHUNK(self));

	/* not changed */
	if (self->bytes == bytes)
		return;

	if (self->bytes != NULL) {
		g_bytes_unref(self->bytes);
		self->bytes = NULL;
	}
	if (bytes != NULL) {
		self->bytes = g_bytes_ref(bytes);
		self->data_sz = g_bytes_get_size(bytes);
	}
}

/**
 * fu_chunk_get_bytes:
 * @self: a #FuChunk
 * @error: (nullable): optional return location for an error
 *
 * Gets the data of the chunk.
 *
 * Returns: (transfer full): data, or %NULL on error
 *
 * Since: 2.0.2
 **/
GBytes *
fu_chunk_get_bytes(FuChunk *self, GError **error)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (self->stream != NULL)
		return fu_input_stream_read_bytes(self->stream, 0x0, G_MAXSIZE, NULL, error);
	if (self->bytes != NULL)
		return g_bytes_ref(self->bytes);
	return g_bytes_new_static(self->data_out, self->data_sz);
}

/**
 * fu_chunk_get_stream:
 * @self: a #FuChunk
 *
 * Gets the data of the chunk.
 *
 * Returns: (transfer full): data
 *
 * Since: 2.0.2
 **/
GInputStream *
fu_chunk_get_stream(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), NULL);
	if (self->stream != NULL)
		return g_object_ref(self->stream);
	if (self->bytes != NULL)
		return g_memory_input_stream_new_from_bytes(self->bytes);
	return g_memory_input_stream_new_from_data(self->data_out, self->data_sz, NULL);
}

static FuChunk *
fu_chunk_new(guint idx, guint page, gsize address, guint8 *data_out, gsize data_sz)
{
	FuChunk *self = g_object_new(FU_TYPE_CHUNK, NULL);
	self->idx = idx;
	self->page = page;
	self->address = address;
	self->data_out = data_out;
	self->data_sz = data_sz;
	return self;
}

/**
 * fu_chunk_bytes_new:
 * @bytes: (nullable): data
 *
 * Creates a new packet of data.
 *
 * Returns: (transfer full): a #FuChunk
 *
 * Since: 1.5.6
 **/
FuChunk *
fu_chunk_bytes_new(GBytes *bytes)
{
	FuChunk *self = g_object_new(FU_TYPE_CHUNK, NULL);
	fu_chunk_set_bytes(self, bytes);
	return self;
}

/**
 * fu_chunk_stream_new:
 * @stream: (nullable): data
 *
 * Creates a new packet of data.
 *
 * Returns: (transfer full): a #FuChunk
 *
 * Since: 2.0.2
 **/
FuChunk *
fu_chunk_stream_new(GInputStream *stream)
{
	FuChunk *self = g_object_new(FU_TYPE_CHUNK, NULL);
	g_set_object(&self->stream, stream);
	return self;
}

void
fu_chunk_export(FuChunk *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kx(bn, "idx", self->idx);
	fu_xmlb_builder_insert_kx(bn, "page", self->page);
	fu_xmlb_builder_insert_kx(bn, "addr", self->address);
	if (self->bytes != NULL) {
		g_autofree gchar *datastr = NULL;
		g_autofree gchar *dataszstr = g_strdup_printf("0x%x", (guint)self->data_sz);
		if (flags & FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA) {
			datastr = fu_memstrsafe(g_bytes_get_data(self->bytes, NULL),
						g_bytes_get_size(self->bytes),
						0x0,
						MIN(self->data_sz, 16),
						NULL);
		} else {
			datastr = g_base64_encode(g_bytes_get_data(self->bytes, NULL),
						  g_bytes_get_size(self->bytes));
		}
		xb_builder_node_insert_text(bn, "data", datastr, "size", dataszstr, NULL);
	}
}

/**
 * fu_chunk_to_string:
 * @self: a #FuChunk
 *
 * Converts the chunked packet to a string representation.
 *
 * Returns: (transfer full): a string
 *
 * Since: 1.1.2
 **/
gchar *
fu_chunk_to_string(FuChunk *self)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("chunk");
	fu_chunk_export(self, FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA, bn);
	return xb_builder_node_export(bn,
				      XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
					  XB_NODE_EXPORT_FLAG_COLLAPSE_EMPTY |
					  XB_NODE_EXPORT_FLAG_FORMAT_INDENT,
				      NULL);
}

/**
 * fu_chunk_array_mutable_new:
 * @data: a mutable blob of memory
 * @data_sz: size of @data_sz
 * @addr_offset: the hardware address offset, or 0
 * @page_sz: the hardware page size, or 0
 * @packet_sz: the transfer size, or 0
 *
 * Chunks a mutable blob of memory into packets, ensuring each packet does not
 * cross a package boundary and is less that a specific transfer size.
 *
 * Returns: (transfer container) (element-type FuChunk): array of packets
 *
 * Since: 1.5.6
 **/
GPtrArray *
fu_chunk_array_mutable_new(guint8 *data,
			   gsize data_sz,
			   gsize addr_offset,
			   gsize page_sz,
			   gsize packet_sz)
{
	GPtrArray *chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	gsize offset = 0;

	g_return_val_if_fail(data != NULL, NULL);
	g_return_val_if_fail(data_sz > 0, NULL);
	g_return_val_if_fail(page_sz == 0 || page_sz >= packet_sz, NULL);

	while (offset < data_sz) {
		gsize chunksz = MIN(packet_sz, data_sz - offset);
		gsize page = 0;
		gsize address_offset = addr_offset + offset;

		/* if page_sz is not specified then all the pages are 0 */
		if (page_sz > 0) {
			address_offset %= page_sz;
			page = (offset + addr_offset) / page_sz;
		}

		/* cut the packet so it does not straddle multiple blocks */
		if (page_sz != packet_sz && page_sz > 0)
			chunksz = MIN(chunksz, (offset + packet_sz) % page_sz);
		g_ptr_array_add(
		    chunks,
		    fu_chunk_new(chunks->len, page, address_offset, data + offset, chunksz));
		offset += chunksz;
	}
	return chunks;
}

/* private */
gboolean
fu_chunk_build(FuChunk *self, XbNode *n, GError **error)
{
	guint64 tmp;
	g_autoptr(XbNode) data = NULL;

	g_return_val_if_fail(FU_IS_CHUNK(self), FALSE);
	g_return_val_if_fail(XB_IS_NODE(n), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "idx", NULL);
	if (tmp != G_MAXUINT64)
		self->idx = tmp;
	tmp = xb_node_query_text_as_uint(n, "page", NULL);
	if (tmp != G_MAXUINT64)
		self->page = tmp;
	tmp = xb_node_query_text_as_uint(n, "addr", NULL);
	if (tmp != G_MAXUINT64)
		self->address = tmp;
	data = xb_node_query_first(n, "data", NULL);
	if (data != NULL && xb_node_get_text(data) != NULL) {
		gsize bufsz = 0;
		g_autofree guchar *buf = NULL;
		g_autoptr(GBytes) blob = NULL;
		buf = g_base64_decode(xb_node_get_text(data), &bufsz);
		blob = g_bytes_new(buf, bufsz);
		fu_chunk_set_bytes(self, blob);
	} else if (data != NULL) {
		g_autoptr(GBytes) blob = NULL;
		blob = g_bytes_new(NULL, 0);
		fu_chunk_set_bytes(self, blob);
	}

	/* success */
	return TRUE;
}

static void
fu_chunk_finalize(GObject *object)
{
	FuChunk *self = FU_CHUNK(object);
	if (self->bytes != NULL)
		g_bytes_unref(self->bytes);
	if (self->stream != NULL)
		g_object_unref(self->stream);
	G_OBJECT_CLASS(fu_chunk_parent_class)->finalize(object);
}

static void
fu_chunk_class_init(FuChunkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_chunk_finalize;
}

static void
fu_chunk_init(FuChunk *self)
{
}
