/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuStreamInputStream"

#include "config.h"

#include "fwupd-rust-cstream.h"

#include "fu-input-stream.h"
#include "fu-stream-input-stream.h"

/**
 * FuStreamInputStream:
 *
 * An input stream that wraps a #GInputStream (or subclass) into the
 * #FuInputStream type hierarchy.
 */

static gssize
fu_stream_input_stream_read_cb(void *handle, guint8 *buf, gsize count, GError **error)
{
	return g_input_stream_read(G_INPUT_STREAM(handle), /* nocheck:blocked */
				   buf,
				   count,
				   NULL,
				   error);
}

static gboolean
fu_stream_input_stream_seek_cb(void *handle, gint64 offset, gint32 type, GError **error)
{
	if (!G_IS_SEEKABLE(handle)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "base stream is not seekable"); /* nocheck:error */
		return FALSE;
	}
	return g_seekable_seek(G_SEEKABLE(handle), offset, (GSeekType)type, NULL, error);
}

static gboolean
fu_stream_input_stream_can_seek_cb(void *handle)
{
	if (!G_IS_SEEKABLE(handle))
		return FALSE;
	return g_seekable_can_seek(G_SEEKABLE(handle));
}

static gint64
fu_stream_input_stream_tell_cb(void *handle)
{
	if (!G_IS_SEEKABLE(handle))
		return 0;
	return g_seekable_tell(G_SEEKABLE(handle));
}

struct _FuStreamInputStream {
	FuInputStream parent_instance;
	GInputStream *base_stream; /* GObject ref to keep alive */ /* nocheck:blocked */
	FuRsCStream *rust;
};

G_DEFINE_TYPE(FuStreamInputStream, fu_stream_input_stream, FU_TYPE_INPUT_STREAM)

static gssize
fu_stream_input_stream_read_fn(FuInputStream *stream,
			       void *buffer,
			       gsize count,
			       GCancellable *cancellable,
			       GError **error)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(stream);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return -1;

	return fu_rs_cstream_read(self->rust, buffer, count, error);
}

static goffset
fu_stream_input_stream_tell(FuInputStream *stream)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(stream);
	return fu_rs_cstream_tell(self->rust);
}

static gboolean
fu_stream_input_stream_can_seek(FuInputStream *stream)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(stream);
	return fu_rs_cstream_can_seek(self->rust);
}

static gboolean
fu_stream_input_stream_seek(FuInputStream *stream,
			    goffset offset,
			    GSeekType type,
			    GCancellable *cancellable,
			    GError **error)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(stream);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;

	return fu_rs_cstream_seek(self->rust, offset, (gint32)type, error);
}

static void
fu_stream_input_stream_finalize(GObject *object)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(object);
	/* must free the Rust stream first */
	fu_rs_cstream_free(self->rust);
	g_clear_object(&self->base_stream);
	G_OBJECT_CLASS(fu_stream_input_stream_parent_class)->finalize(object);
}

static FuRsStreamImpl *
fu_stream_input_stream_get_stream_impl(FuInputStream *stream)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(stream);
	return fu_rs_cstream_get_stream_impl(self->rust);
}

static void
fu_stream_input_stream_class_init(FuStreamInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuInputStreamClass *istream_class = FU_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fu_stream_input_stream_finalize;
	istream_class->read_fn = fu_stream_input_stream_read_fn;
	istream_class->tell = fu_stream_input_stream_tell;
	istream_class->can_seek = fu_stream_input_stream_can_seek;
	istream_class->seek = fu_stream_input_stream_seek;
	istream_class->get_stream_impl = fu_stream_input_stream_get_stream_impl;
}

static void
fu_stream_input_stream_init(FuStreamInputStream *self)
{
}

/**
 * fu_stream_input_stream_from_stream:
 * @stream: (transfer none): a #GInputStream
 *
 * Wraps a #GInputStream and returns a #FuInputStream. The @stream
 * is reffed and future operations on this input stream apply to the
 * underlying #GInputStream.
 *
 * Returns: (transfer full): a #FuInputStream, or %NULL on error
 *
 * Since: 2.0.7
 **/
FuInputStream *
fu_stream_input_stream_from_stream(GInputStream *stream)
{
	FuRsStreamCallbacks callbacks = {
	    .read_fn = fu_stream_input_stream_read_cb,
	    .seek_fn = fu_stream_input_stream_seek_cb,
	    .can_seek_fn = fu_stream_input_stream_can_seek_cb,
	    .tell_fn = fu_stream_input_stream_tell_cb,
	    .destroy_fn = NULL,
	};
	g_autoptr(FuStreamInputStream) self = NULL;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);

	self = g_object_new(FU_TYPE_STREAM_INPUT_STREAM, NULL);
	self->base_stream = g_object_ref(stream);
	self->rust = fu_rs_cstream_new(stream, callbacks);

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}
