/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCompositeInputStream"

#include "config.h"

#include "fu-composite-input-stream.h"

/**
 * FuCompositeInputStream:
 *
 * A input stream that is made up of other partial streams, e.g.
 *
 *       off    sz     off  sz
 *    [xxxxxxxxxxxx] [yyyyyyyy]
 *       |  0x6  |    |0x4|
 *        \      \   /   /
 *         \      \ /   /
 *          \      |    |
 *           |     |    |
 *          [xxxxxxyyyyyy]
 *
 * xxx offset: 2, sz: 6
 * yyy offset: 0, sz: 4
 */

typedef struct {
	FuPartialInputStream *partial_stream;
	gsize global_offset;
} FuCompositeInputStreamItem;

struct _FuCompositeInputStream {
	GInputStream parent_instance;
	GPtrArray *items; /* of FuCompositeInputStreamItem */
	FuCompositeInputStreamItem *last_item; /* no-ref */
	goffset pos;
	goffset pos_offset;
	gsize total_size;
};

static void
fu_composite_input_stream_seekable_iface_init(GSeekableIface *iface);

G_DEFINE_TYPE_WITH_CODE(FuCompositeInputStream,
			fu_composite_input_stream,
			G_TYPE_INPUT_STREAM,
			G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
					      fu_composite_input_stream_seekable_iface_init))

static void
fu_composite_input_stream_item_free(FuCompositeInputStreamItem *item)
{
	g_object_unref(item->partial_stream);
	g_free(item);
}

/**
 * fu_composite_input_stream_add_bytes:
 * @self: a #FuCompositeInputStream
 * @bytes: a #GBytes
 *
 * Adds a bytes object.
 *
 * Since: 1.9.11
 **/
void
fu_composite_input_stream_add_bytes(FuCompositeInputStream *self, GBytes *bytes)
{
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	g_return_if_fail(FU_IS_COMPOSITE_INPUT_STREAM(self));
	g_return_if_fail(bytes != NULL);

	stream = g_memory_input_stream_new_from_bytes(bytes);
	partial_stream = fu_partial_input_stream_new(stream, 0x0, g_bytes_get_size(bytes));
	fu_composite_input_stream_add_partial_stream(self, FU_PARTIAL_INPUT_STREAM(partial_stream));
}

/**
 * fu_composite_input_stream_add_partial_stream:
 * @self: a #FuCompositeInputStream
 * @partial_stream: a #FuPartialInputStream
 *
 * Adds a partial stream object.
 *
 * Since: 1.9.11
 **/
void
fu_composite_input_stream_add_partial_stream(FuCompositeInputStream *self,
					     FuPartialInputStream *partial_stream)
{
	FuCompositeInputStreamItem *item;
	gsize global_offset = 0;

	g_return_if_fail(FU_IS_COMPOSITE_INPUT_STREAM(self));
	g_return_if_fail(FU_IS_PARTIAL_INPUT_STREAM(partial_stream));
	g_return_if_fail(G_INPUT_STREAM(self) != G_INPUT_STREAM(partial_stream));

	/* get the last-added item */
	if (self->items->len > 0) {
		FuCompositeInputStreamItem *item_last =
		    g_ptr_array_index(self->items, self->items->len - 1);
		global_offset = item_last->global_offset +
				fu_partial_input_stream_get_size(
				    FU_PARTIAL_INPUT_STREAM(item_last->partial_stream));
	}

	/* add a new item */
	item = g_new0(FuCompositeInputStreamItem, 1);
	item->partial_stream = g_object_ref(partial_stream);
	item->global_offset = global_offset;

	g_debug("adding partial stream global_offset:0x%x", (guint)item->global_offset);
	self->total_size += fu_partial_input_stream_get_size(item->partial_stream);
	g_ptr_array_add(self->items, item);
}

static goffset
fu_composite_input_stream_tell(GSeekable *seekable)
{
	FuCompositeInputStream *self = FU_COMPOSITE_INPUT_STREAM(seekable);
	g_return_val_if_fail(FU_IS_COMPOSITE_INPUT_STREAM(self), -1);
	return self->pos;
}

static gboolean
fu_composite_input_stream_can_seek(GSeekable *seekable)
{
	return TRUE;
}

static FuCompositeInputStreamItem *
fu_composite_input_stream_get_item_for_offset(FuCompositeInputStream *self,
					      gsize offset,
					      GError **error)
{
	for (guint i = 0; i < self->items->len; i++) {
		FuCompositeInputStreamItem *item = g_ptr_array_index(self->items, i);
		gsize item_size = fu_partial_input_stream_get_size(item->partial_stream);
		if (offset < item->global_offset + item_size)
			return item;
	}
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "offset is 0x%x out of range",
		    (guint)offset);
	return NULL;
}

static gboolean
fu_composite_input_stream_seek(GSeekable *seekable,
			       goffset offset,
			       GSeekType type,
			       GCancellable *cancellable,
			       GError **error)
{
	FuCompositeInputStream *self = FU_COMPOSITE_INPUT_STREAM(seekable);

	g_return_val_if_fail(FU_IS_COMPOSITE_INPUT_STREAM(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* reset */
	self->pos_offset = 0;
	self->last_item = NULL;

	if (type == G_SEEK_CUR) {
		self->pos += offset;
	} else if (type == G_SEEK_END) {
		self->pos = self->total_size + offset;
	} else {
		self->pos = offset;
	}
	return TRUE;
}

static gboolean
fu_composite_input_stream_can_truncate(GSeekable *seekable)
{
	return FALSE;
}

static gboolean
fu_composite_input_stream_truncate(GSeekable *seekable,
				   goffset offset,
				   GCancellable *cancellable,
				   GError **error)
{
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot truncate FuCompositeInputStream");
	return FALSE;
}

static void
fu_composite_input_stream_seekable_iface_init(GSeekableIface *iface)
{
	iface->tell = fu_composite_input_stream_tell;
	iface->can_seek = fu_composite_input_stream_can_seek;
	iface->seek = fu_composite_input_stream_seek;
	iface->can_truncate = fu_composite_input_stream_can_truncate;
	iface->truncate_fn = fu_composite_input_stream_truncate;
}

/**
 * fu_composite_input_stream_new:
 *
 * Creates a composite input stream.
 *
 * Returns: (transfer full): a #FuCompositeInputStream
 *
 * Since: 1.9.11
 **/
GInputStream *
fu_composite_input_stream_new(void)
{
	return G_INPUT_STREAM(g_object_new(FU_TYPE_COMPOSITE_INPUT_STREAM, NULL));
}

static gssize
fu_composite_input_stream_read(GInputStream *stream,
			       void *buffer,
			       gsize count,
			       GCancellable *cancellable,
			       GError **error)
{
	FuCompositeInputStream *self = FU_COMPOSITE_INPUT_STREAM(stream);
	FuCompositeInputStreamItem *item;
	gssize rc;

	g_return_val_if_fail(FU_IS_COMPOSITE_INPUT_STREAM(self), -1);
	g_return_val_if_fail(error == NULL || *error == NULL, -1);

	item =
	    fu_composite_input_stream_get_item_for_offset(self, self->pos + self->pos_offset, NULL);
	if (item == NULL)
		return 0;
	if (item != self->last_item) {
		if (!g_seekable_seek(G_SEEKABLE(item->partial_stream),
				     self->pos + self->pos_offset - item->global_offset,
				     G_SEEK_SET,
				     cancellable,
				     error))
			return -1;
		self->last_item = item;
	}
	rc = g_input_stream_read(G_INPUT_STREAM(item->partial_stream),
				 buffer,
				 count,
				 cancellable,
				 error);
	if (rc < 0)
		return rc;

	/* we have to keep track of this in case we have to switch the FuCompositeInputStreamItem
	 * without an explicit seek */
	self->pos_offset += rc;
	return rc;
}

static void
fu_composite_input_stream_finalize(GObject *object)
{
	FuCompositeInputStream *self = FU_COMPOSITE_INPUT_STREAM(object);
	g_ptr_array_unref(self->items);
	G_OBJECT_CLASS(fu_composite_input_stream_parent_class)->finalize(object);
}

static void
fu_composite_input_stream_class_init(FuCompositeInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GInputStreamClass *istream_class = G_INPUT_STREAM_CLASS(klass);
	istream_class->read_fn = fu_composite_input_stream_read;
	object_class->finalize = fu_composite_input_stream_finalize;
}

static void
fu_composite_input_stream_init(FuCompositeInputStream *self)
{
	self->items =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_composite_input_stream_item_free);
}
