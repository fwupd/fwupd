/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMemoryInputStream"

#include "config.h"

#include "fwupd-rust-borrowed-memory-input-stream.h"

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
	FuRsBorrowedMemoryInputStream *rust;
	GBytes *bytes;
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
	gssize rc;

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return -1;

	rc = fu_rs_borrowed_memory_input_stream_read(self->rust, buffer, count);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(-rc),
#else
			    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
			    "failed to read %zu bytes: %s",
			    count,
			    fwupd_strerror(-rc));
		fwupd_error_convert(error);
		return -1;
	}
	return rc;
}

static goffset
fu_memory_input_stream_tell(FuInputStream *stream)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);
	return fu_rs_borrowed_memory_input_stream_tell(self->rust);
}

static gboolean
fu_memory_input_stream_can_seek(FuInputStream *stream)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);
	return fu_rs_borrowed_memory_input_stream_can_seek(self->rust);
}

static gboolean
fu_memory_input_stream_seek(FuInputStream *stream,
			    goffset offset,
			    GSeekType type,
			    GCancellable *cancellable,
			    GError **error)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);

	switch (type) {
	case G_SEEK_SET:
	case G_SEEK_CUR:
	case G_SEEK_END:
		break;
	default:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported seek type");
		return FALSE;
	}

	if (!fu_rs_borrowed_memory_input_stream_seek(self->rust, offset, (gint32)type)) {
		if (offset >= 0)
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "seek to 0x%" G_GINT64_MODIFIER "x failed",
				    (guint64)offset); /* nocheck:error */
		else
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "seek to %" G_GINT64_MODIFIER "d failed",
				    offset); /* nocheck:error */
		fwupd_error_convert(error);
		return FALSE;
	}
	return TRUE;
}

static void
fu_memory_input_stream_finalize(GObject *object)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(object);
	/* free the Rust stream first */
	fu_rs_borrowed_memory_input_stream_free(self->rust);
	g_clear_pointer(&self->bytes, g_bytes_unref);
	G_OBJECT_CLASS(fu_memory_input_stream_parent_class)->finalize(object);
}

static FuRsStreamImpl *
fu_memory_input_stream_get_stream_impl(FuInputStream *stream)
{
	FuMemoryInputStream *self = FU_MEMORY_INPUT_STREAM(stream);
	return fu_rs_borrowed_memory_input_stream_get_stream_impl(self->rust);
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
	istream_class->get_stream_impl = fu_memory_input_stream_get_stream_impl;
}

static void
fu_memory_input_stream_init(FuMemoryInputStream *self)
{
	self->rust = NULL;
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
	gsize data_sz = 0;
	const guint8 *data;
	g_autoptr(FuMemoryInputStream) self = NULL;

	g_return_val_if_fail(bytes != NULL, NULL);

	self = g_object_new(FU_TYPE_MEMORY_INPUT_STREAM, NULL);

	data = g_bytes_get_data(bytes, &data_sz);

	self->rust = fu_rs_borrowed_memory_input_stream_new_from_data(data, data_sz);
	/* we must hold a reference to the bytes because we cannot know if the
	 * caller uses the same bytes for something. We can't extract the data
	 * either because we cannot know if there's a free_func set on the
	 * GBytes */
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
