/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuChunk"

#include "config.h"

#include "fu-chunk-private.h"
#include "fu-common.h"
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
	const guint8 *data;
	gsize data_sz;
	gboolean is_mutable;
	GBytes *bytes;
};

G_DEFINE_TYPE(FuChunk, fu_chunk, G_TYPE_OBJECT)

/**
 * fu_chunk_set_idx:
 * @self: a #FuChunk
 * @idx: index, starting at 0
 *
 * Sets the index of the chunk.
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
 **/
guint
fu_chunk_get_idx(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXUINT);
	return self->idx;
}

/**
 * fu_chunk_set_page:
 * @self: a #FuChunk
 * @page: page number, starting at 0
 *
 * Sets the page of the chunk.
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
 **/
guint
fu_chunk_get_page(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXUINT);
	return self->page;
}

/**
 * fu_chunk_set_address:
 * @self: a #FuChunk
 * @address: memory address
 *
 * Sets the address of the chunk.
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
 **/
gsize
fu_chunk_get_address(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXSIZE);
	return self->address;
}

/**
 * fu_chunk_get_data:
 * @self: a #FuChunk
 *
 * Gets the data of the chunk.
 *
 * Returns: bytes
 **/
const guint8 *
fu_chunk_get_data(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), NULL);
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
 * Returns: (transfer none): bytes
 **/
guint8 *
fu_chunk_get_data_out(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), NULL);

	/* warn, but allow to proceed */
	if (!self->is_mutable) {
		g_critical("calling fu_chunk_get_data_out() from immutable chunk");
		self->is_mutable = TRUE;
	}
	return (guint8 *)self->data;
}

/**
 * fu_chunk_get_data_sz:
 * @self: a #FuChunk
 *
 * Gets the data size of the chunk.
 *
 * Returns: size in bytes
 **/
gsize
fu_chunk_get_data_sz(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), G_MAXSIZE);
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
 **/
void
fu_chunk_set_bytes(FuChunk *self, GBytes *bytes)
{
	g_return_if_fail(FU_IS_CHUNK(self));

	/* not changed */
	if (self->bytes == bytes)
		return;

	g_clear_pointer(&self->bytes, g_bytes_unref);
	if (bytes != NULL) {
		self->bytes = g_bytes_ref(bytes);
		self->data = g_bytes_get_data(bytes, NULL);
		self->data_sz = g_bytes_get_size(bytes);
	}
}

/**
 * fu_chunk_get_bytes:
 * @self: a #FuChunk
 *
 * Gets the data of the chunk.
 *
 * Returns: (transfer full): data
 **/
GBytes *
fu_chunk_get_bytes(FuChunk *self)
{
	g_return_val_if_fail(FU_IS_CHUNK(self), NULL);
	if (self->bytes != NULL)
		return g_bytes_ref(self->bytes);
	return g_bytes_new_static(self->data, self->data_sz);
}

/**
 * fu_chunk_new:
 * @idx: the packet number
 * @page: the hardware memory page
 * @address: the address *within* the page
 * @data: the data
 * @data_sz: size of @data_sz
 *
 * Creates a new packet of chunked data.
 *
 * Returns: (transfer full): a #FuChunk
 **/
FuChunk *
fu_chunk_new(guint idx, guint page, gsize address, const guint8 *data, gsize data_sz)
{
	FuChunk *self = g_object_new(FU_TYPE_CHUNK, NULL);
	self->idx = idx;
	self->page = page;
	self->address = address;
	self->data = data;
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
 **/
FuChunk *
fu_chunk_bytes_new(GBytes *bytes)
{
	FuChunk *self = g_object_new(FU_TYPE_CHUNK, NULL);
	fu_chunk_set_bytes(self, bytes);
	return self;
}

void
fu_chunk_export(FuChunk *self, FuFirmwareExportFlags flags, FwupdJsonObject *json_object)
{
	if (self->idx != 0)
		fwupd_json_object_add_integer(json_object, "idx", self->idx);
	if (self->page != 0)
		fwupd_json_object_add_integer(json_object, "page", self->page);
	if (self->address != 0)
		fwupd_json_object_add_integer(json_object, "addr", (gint64)self->address);
	if (self->data != NULL) {
		g_autofree gchar *datastr = NULL;
		if (flags & FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA) {
			datastr = fu_memstrsafe(self->data,
						self->data_sz,
						0x0,
						MIN(self->data_sz, 16),
						NULL);
		} else {
			datastr = fu_base64_encode(self->data, self->data_sz);
		}
		if (datastr != NULL)
			fwupd_json_object_add_string(json_object, "data", datastr);
		fwupd_json_object_add_integer(json_object, "data_size", (gint64)self->data_sz);
	} else {
		if (self->data_sz != 0)
			fwupd_json_object_add_integer(json_object, "size", (gint64)self->data_sz);
	}
}

/**
 * fu_chunk_to_string:
 * @self: a #FuChunk
 *
 * Converts the chunked packet to a string representation.
 *
 * Returns: (transfer full): a string
 **/
gchar *
fu_chunk_to_string(FuChunk *self)
{
	g_autoptr(FwupdJsonObject) json_object = fwupd_json_object_new();
	g_autoptr(GString) str = NULL;
	fu_chunk_export(self, FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA, json_object);
	str = fwupd_json_object_to_string(json_object, FWUPD_JSON_EXPORT_FLAG_INDENT);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_chunk_array_to_string:
 * @chunks: (element-type FuChunk): array of chunks
 *
 * Converts all the chunked packets in an array to a string representation.
 *
 * Returns: (transfer full): a string
 **/
gchar *
fu_chunk_array_to_string(GPtrArray *chunks)
{
	g_autoptr(FwupdJsonArray) json_array = fwupd_json_array_new();
	g_autoptr(GString) str = NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(FwupdJsonObject) json_object = fwupd_json_object_new();
		fu_chunk_export(chk, FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA, json_object);
		fwupd_json_array_add_object(json_array, json_object);
	}
	str = fwupd_json_array_to_string(json_array, FWUPD_JSON_EXPORT_FLAG_INDENT);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_chunk_array_mutable_new:
 * @data: a mutable blob of memory
 * @data_sz: size of @data_sz
 * @addr_offset: the hardware address offset, or 0
 * @page_sz: the hardware page size, or 0
 * @packet_sz: the transfer size, or 0
 * @error: (nullable): optional return location for an error
 *
 * Chunks a mutable blob of memory into packets, ensuring each packet does not
 * cross a package boundary and is less that a specific transfer size.
 *
 * Returns: (transfer container) (element-type FuChunk): array of packets
 **/
GPtrArray *
fu_chunk_array_mutable_new(guint8 *data,
			   gsize data_sz,
			   gsize addr_offset,
			   gsize page_sz,
			   gsize packet_sz,
			   GError **error)
{
	GPtrArray *chunks;

	g_return_val_if_fail(data != NULL, NULL);
	g_return_val_if_fail(data_sz > 0, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	chunks = fu_chunk_array_new(data, data_sz, addr_offset, page_sz, packet_sz, error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		chk->is_mutable = TRUE;
	}
	return chunks;
}

/**
 * fu_chunk_array_new:
 * @data: (nullable): an optional linear blob of memory
 * @data_sz: size of @data_sz
 * @addr_offset: the hardware address offset, or 0
 * @page_sz: the hardware page size, or 0
 * @packet_sz: the transfer size, or 0
 * @error: (nullable): optional return location for an error
 *
 * Chunks a linear blob of memory into packets, ensuring each packet does not
 * cross a package boundary and is less that a specific transfer size.
 *
 * Returns: (transfer container) (element-type FuChunk): array of packets, or %NULL
 **/
GPtrArray *
fu_chunk_array_new(const guint8 *data,
		   gsize data_sz,
		   gsize addr_offset,
		   gsize page_sz,
		   gsize packet_sz,
		   GError **error)
{
	gsize offset = 0;
	g_autoptr(GPtrArray) chunks =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(page_sz == 0 || page_sz >= packet_sz, NULL);
	g_return_val_if_fail(packet_sz > 0, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	while (offset < data_sz) {
		gsize chunksz = MIN(packet_sz, data_sz - offset);
		gsize page = 0;
		gsize address_offset = addr_offset;

		/* if page_sz is not specified then all the pages are 0 */
		if (!fu_size_checked_inc(&address_offset, offset, error))
			return NULL;
		if (page_sz > 0) {
			gsize offset_next = offset;

			address_offset %= page_sz;
			if (!fu_size_checked_inc(&offset_next, addr_offset, error))
				return NULL;
			page = offset_next / page_sz;
		}

		/* cut the packet so it does not straddle multiple pages */
		if (page_sz > 0) {
			gsize offset_abs = offset;

			if (!fu_size_checked_inc(&offset_abs, addr_offset, error))
				return NULL;
			chunksz = MIN(chunksz, page_sz - (offset_abs % page_sz));
		}
		g_ptr_array_add(chunks,
				fu_chunk_new(chunks->len,
					     page,
					     address_offset,
					     data != NULL ? data + offset : NULL,
					     chunksz));
		if (!fu_size_checked_inc(&offset, chunksz, error))
			return NULL;
	}

#ifndef SUPPORTED_BUILD
	/* show the programmer a warning */
	if (page_sz == 0x0 && chunks->len > 10000) {
		g_warning("fu_chunk_array_new() generated a lot of chunks (%u), "
			  "maybe use FuChunkArray instead?",
			  chunks->len);
	}
#endif
	return g_steal_pointer(&chunks);
}

/* private */
gboolean
fu_chunk_build(FuChunk *self, FwupdJsonObject *json_object, GError **error)
{
	gint64 tmp = G_MAXINT64;

	g_return_val_if_fail(FU_IS_CHUNK(self), FALSE);
	g_return_val_if_fail(json_object != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional properties */
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"idx",
							&tmp,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmp != G_MAXINT64)
		self->idx = (guint32)tmp;
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"page",
							&tmp,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmp != G_MAXINT64)
		self->page = (guint32)tmp;
	if (!fwupd_json_object_get_integer_with_default(json_object,
							"addr",
							&tmp,
							G_MAXINT64,
							error))
		return FALSE;
	if (tmp != G_MAXINT64)
		self->address = (gsize)tmp;
	if (fwupd_json_object_has_node(json_object, "data")) {
		const gchar *data = NULL;
		g_autoptr(GBytes) blob = NULL;
		if (!fwupd_json_object_get_string_optional(json_object, "data", &data, error))
			return FALSE;
		if (data != NULL) {
			gsize bufsz = 0;
			g_autofree guchar *buf = g_base64_decode(data, &bufsz);
			blob = g_bytes_new(buf, bufsz);
		} else {
			blob = g_bytes_new(NULL, 0);
		}
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
