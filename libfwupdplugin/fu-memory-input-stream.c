/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMemoryInputStream"

#include "config.h"

#include "fu-mem.h"
#include "fu-memory-input-stream.h"

/**
 * FuMemoryInputStream:
 *
 * A memory-backed input stream that wraps a #GBytes, the
 * fwupd equivalent to #GMemoryInputStream. This implementation
 * is a drop-in replacement for #GMemoryInputStream for the needs
 * within fwupd.
 */
struct _FuMemoryInputStream {
	FuInputStream parent_instance;
	GBytes *bytes;
	gsize pos;
};

G_DEFINE_TYPE(FuMemoryInputStream, fu_memory_input_stream, FU_TYPE_INPUT_STREAM)

static gssize
fu_memory_input_stream_read_fn(FuInputStream *stream,
			       void *buffer,
			       gsize count,
			       GCancellable *cancellable,
			       GError **error)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);
	gsize data_sz = 0;
	const guint8 *data = g_bytes_get_data(self->bytes, &data_sz);
	gsize available;

	if (self->pos >= data_sz)
		return 0;

	available = data_sz - self->pos;
	count = MIN(count, available);
	if (count > 0) {
		if (!fu_memcpy_safe(buffer, count, 0x0, data, data_sz, self->pos, count, error))
			return -1;
	}
	self->pos += count;
	return (gssize)count;
}

static goffset
fu_memory_input_stream_tell(FuInputStream *stream)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);
	return (goffset)self->pos;
}

static gboolean
fu_memory_input_stream_can_seek(FuInputStream *stream)
{
	return TRUE;
}

static gboolean
fu_memory_input_stream_seek(FuInputStream *stream,
			    goffset offset,
			    GSeekType type,
			    GCancellable *cancellable,
			    GError **error)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);
	gsize data_sz = g_bytes_get_size(self->bytes);
	goffset new_pos;

	switch (type) {
	case G_SEEK_SET:
		new_pos = offset;
		break;
	case G_SEEK_CUR:
		new_pos = (goffset)self->pos + offset;
		break;
	case G_SEEK_END:
		new_pos = (goffset)data_sz + offset;
		break;
	default:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported seek type");
		return FALSE;
	}

	if (new_pos < 0 || (gsize)new_pos > data_sz) {
		g_set_error(error, /* nocheck:error */
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "seek to %" G_GINT64_MODIFIER
			    "d is outside stream of size 0x%" G_GSIZE_MODIFIER "x",
			    (gint64)new_pos,
			    data_sz);
		return FALSE;
	}

	self->pos = (gsize)new_pos;
	return TRUE;
}

static void
fu_memory_input_stream_finalize(GObject *object)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(object);
	g_clear_pointer(&self->bytes, g_bytes_unref);
	G_OBJECT_CLASS(fu_memory_input_stream_parent_class)->finalize(object);
}

static void
fu_memory_input_stream_class_init(FuMemoryInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuInputStreamClass *istream_class = FU_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fu_memory_input_stream_finalize;
	istream_class->read_fn = fu_memory_input_stream_read_fn;
	istream_class->tell = fu_memory_input_stream_tell;
	istream_class->can_seek = fu_memory_input_stream_can_seek;
	istream_class->seek = fu_memory_input_stream_seek;
}

static void
fu_memory_input_stream_init(FuMemoryInputStream *self)
{
	self->bytes = g_bytes_new(NULL, 0);
}

/**
 * fu_memory_input_stream_new:
 *
 * Creates a new empty memory-backed input stream.
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.7
 **/
FuInputStream *
fu_memory_input_stream_new(void)
{
	FuMemoryInputStream *self = g_object_new(FU_TYPE_MEMORY_INPUT_STREAM, NULL);

	return FU_INPUT_STREAM(self);
}

/**
 * fu_memory_input_stream_new_from_bytes:
 * @bytes: a #GBytes
 *
 * Creates a new memory-backed input stream from @bytes.
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.7
 **/
FuInputStream *
fu_memory_input_stream_new_from_bytes(GBytes *bytes)
{
	g_autoptr(FuMemoryInputStream) self = NULL;

	g_return_val_if_fail(bytes != NULL, NULL);

	self = g_object_new(FU_TYPE_MEMORY_INPUT_STREAM, NULL);
	g_clear_pointer(&self->bytes, g_bytes_unref);
	self->bytes = g_bytes_ref(bytes);

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}

/**
 * fu_memory_input_stream_new_from_data:
 * @data: (array length=len) (element-type guint8): input data
 * @len: length of the data, or -1 if @data is a nul-terminated string
 * @destroy: (nullable): function that is called to free @data, or %NULL
 *
 * Creates a new memory-backed input stream from @data.
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.7
 **/
FuInputStream *
fu_memory_input_stream_new_from_data(const void *data, gssize len, GDestroyNotify destroy)
{
	g_autoptr(GBytes) bytes = NULL;

	g_return_val_if_fail(data != NULL, NULL);
	g_return_val_if_fail(len >= -1, NULL);

	if (len == -1)
		len = strlen(data);

	bytes = g_bytes_new_with_free_func(data, len, destroy, (gpointer)data);

	return fu_memory_input_stream_new_from_bytes(bytes);
}
