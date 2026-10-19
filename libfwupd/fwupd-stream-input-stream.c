/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-rust-cstream.h"
#include "fwupd-stream-input-stream-private.h"

/**
 * FwupdStreamInputStream:
 *
 * An input stream that wraps a #GInputStream (or subclass) into the
 * #FwupdInputStream type hierarchy.
 *
 * This is private API and is only exported for use by libfwupd.
 */

static gssize
fwupd_stream_input_stream_read_cb(void *handle, guint8 *buf, gsize count, GError **error)
{
	return g_input_stream_read(G_INPUT_STREAM(handle), /* nocheck:blocked */
				   buf,
				   count,
				   NULL,
				   error);
}

static gboolean
fwupd_stream_input_stream_seek_cb(void *handle, gint64 offset, gint32 type, GError **error)
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
fwupd_stream_input_stream_can_seek_cb(void *handle)
{
	if (!G_IS_SEEKABLE(handle))
		return FALSE;
	return g_seekable_can_seek(G_SEEKABLE(handle));
}

static gint64
fwupd_stream_input_stream_tell_cb(void *handle)
{
	if (!G_IS_SEEKABLE(handle))
		return 0;
	return g_seekable_tell(G_SEEKABLE(handle));
}

struct _FwupdStreamInputStream {
	FwupdInputStream parent_instance;
	GInputStream *base_stream; /* GObject ref to keep alive */ /* nocheck:blocked */
	FuRsCStream *rust;
};

G_DEFINE_TYPE(FwupdStreamInputStream, fwupd_stream_input_stream, FWUPD_TYPE_INPUT_STREAM)

static gssize
fwupd_stream_input_stream_read_fn(FwupdInputStream *stream,
				  void *buffer,
				  gsize count,
				  GCancellable *cancellable,
				  GError **error)
{
	FwupdStreamInputStream *self = FWUPD_STREAM_INPUT_STREAM(stream);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return -1;

	return fu_rs_cstream_read(self->rust, buffer, count, error);
}

static goffset
fwupd_stream_input_stream_tell(FwupdInputStream *stream)
{
	FwupdStreamInputStream *self = FWUPD_STREAM_INPUT_STREAM(stream);
	return fu_rs_cstream_tell(self->rust);
}

static gboolean
fwupd_stream_input_stream_can_seek(FwupdInputStream *stream)
{
	FwupdStreamInputStream *self = FWUPD_STREAM_INPUT_STREAM(stream);
	return fu_rs_cstream_can_seek(self->rust);
}

static gboolean
fwupd_stream_input_stream_seek(FwupdInputStream *stream,
			       goffset offset,
			       GSeekType type,
			       GCancellable *cancellable,
			       GError **error)
{
	FwupdStreamInputStream *self = FWUPD_STREAM_INPUT_STREAM(stream);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;

	return fu_rs_cstream_seek(self->rust, offset, (gint32)type, error);
}

static void
fwupd_stream_input_stream_finalize(GObject *object)
{
	FwupdStreamInputStream *self = FWUPD_STREAM_INPUT_STREAM(object);
	/* must free the Rust stream first */
	fu_rs_cstream_free(self->rust);
	g_clear_object(&self->base_stream);
	G_OBJECT_CLASS(fwupd_stream_input_stream_parent_class)->finalize(object);
}

static FwupdStreamImpl
fwupd_stream_input_stream_get_stream_impl(FwupdInputStream *stream)
{
	FwupdStreamInputStream *self = FWUPD_STREAM_INPUT_STREAM(stream);
	return fu_rs_cstream_get_stream_impl(self->rust);
}

static void
fwupd_stream_input_stream_class_init(FwupdStreamInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FwupdInputStreamClass *istream_class = FWUPD_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fwupd_stream_input_stream_finalize;
	istream_class->read_fn = fwupd_stream_input_stream_read_fn;
	istream_class->tell = fwupd_stream_input_stream_tell;
	istream_class->can_seek = fwupd_stream_input_stream_can_seek;
	istream_class->seek = fwupd_stream_input_stream_seek;
	istream_class->get_stream_impl = fwupd_stream_input_stream_get_stream_impl;
}

static void
fwupd_stream_input_stream_init(FwupdStreamInputStream *self)
{
}

/**
 * fwupd_stream_input_stream_new: (skip):
 * @stream: (transfer none): a #GInputStream
 *
 * Wraps a #GInputStream and returns a #FwupdInputStream superclass.
 * The @stream is reffed and operations on this stream now apply to the underlying #GInputStream.
 *
 * Returns: (transfer full): a #FwupdStreamInputStream, or %NULL on error
 *
 * Since: 2.2.1
 **/
FwupdStreamInputStream *
fwupd_stream_input_stream_new(GInputStream *stream)
{
	FuRsStreamCallbacks callbacks = {
	    .read_fn = fwupd_stream_input_stream_read_cb,
	    .seek_fn = fwupd_stream_input_stream_seek_cb,
	    .can_seek_fn = fwupd_stream_input_stream_can_seek_cb,
	    .tell_fn = fwupd_stream_input_stream_tell_cb,
	    .destroy_fn = NULL,
	};
	g_autoptr(FwupdStreamInputStream) self = NULL;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);

	self = g_object_new(FWUPD_TYPE_STREAM_INPUT_STREAM, NULL);
	self->base_stream = g_object_ref(stream);
	self->rust = fu_rs_cstream_new(stream, callbacks);

	return g_steal_pointer(&self);
}
