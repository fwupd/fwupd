/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuStreamInputStream"

#include "config.h"

#include "fu-stream-input-stream-private.h"
#include "fu-stream-input-stream.h"

/**
 * FuStreamInputStream:
 *
 * An input stream that wraps a #GInputStream (or subclass) into the
 * #FuInputStream type hierarchy.
 */

typedef struct {
	GInputStream *base_stream; /* nocheck:blocked */
} FuStreamInputStreamPrivate;

static void
fu_stream_input_stream_seekable_iface_init(GSeekableIface *iface);

G_DEFINE_TYPE_WITH_CODE(FuStreamInputStream,
			fu_stream_input_stream,
			FU_TYPE_INPUT_STREAM,
			G_ADD_PRIVATE(FuStreamInputStream)
			    G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
						  fu_stream_input_stream_seekable_iface_init))

static gssize
fu_stream_input_stream_read(GInputStream *stream,
			    void *buffer,
			    gsize count,
			    GCancellable *cancellable,
			    GError **error)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(stream);
	FuStreamInputStreamPrivate *priv = fu_stream_input_stream_get_instance_private(self);

	g_return_val_if_fail(priv->base_stream != NULL, -1);

	return g_input_stream_read(priv->base_stream, buffer, count, cancellable, error);
}

static goffset
fu_stream_input_stream_tell(GSeekable *seekable)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(seekable);
	FuStreamInputStreamPrivate *priv = fu_stream_input_stream_get_instance_private(self);

	if (!G_IS_SEEKABLE(priv->base_stream))
		return 0;

	return g_seekable_tell(G_SEEKABLE(priv->base_stream));
}

static gboolean
fu_stream_input_stream_can_seek(GSeekable *seekable)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(seekable);
	FuStreamInputStreamPrivate *priv = fu_stream_input_stream_get_instance_private(self);

	if (!G_IS_SEEKABLE(priv->base_stream))
		return FALSE;

	return g_seekable_can_seek(G_SEEKABLE(priv->base_stream));
}

static gboolean
fu_stream_input_stream_seek(GSeekable *seekable,
			    goffset offset,
			    GSeekType type,
			    GCancellable *cancellable,
			    GError **error)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(seekable);
	FuStreamInputStreamPrivate *priv = fu_stream_input_stream_get_instance_private(self);

	if (!G_IS_SEEKABLE(priv->base_stream)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "base stream is not seekable"); /* nocheck:error */
		return FALSE;
	}

	return g_seekable_seek(G_SEEKABLE(priv->base_stream), offset, type, cancellable, error);
}

static gboolean
fu_stream_input_stream_can_truncate(GSeekable *seekable)
{
	return FALSE;
}

static gboolean
fu_stream_input_stream_truncate(GSeekable *seekable,
				goffset offset,
				GCancellable *cancellable,
				GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot truncate FuStreamInputStream");
	return FALSE;
}

static void
fu_stream_input_stream_seekable_iface_init(GSeekableIface *iface)
{
	iface->tell = fu_stream_input_stream_tell;
	iface->can_seek = fu_stream_input_stream_can_seek;
	iface->seek = fu_stream_input_stream_seek;
	iface->can_truncate = fu_stream_input_stream_can_truncate;
	iface->truncate_fn = fu_stream_input_stream_truncate;
}

static void
fu_stream_input_stream_finalize(GObject *object)
{
	FuStreamInputStream *self = FU_STREAM_INPUT_STREAM(object);
	FuStreamInputStreamPrivate *priv = fu_stream_input_stream_get_instance_private(self);
	g_clear_object(&priv->base_stream);
	G_OBJECT_CLASS(fu_stream_input_stream_parent_class)->finalize(object);
}

static void
fu_stream_input_stream_class_init(FuStreamInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GInputStreamClass *istream_class = G_INPUT_STREAM_CLASS(klass); /* nocheck:blocked */
	object_class->finalize = fu_stream_input_stream_finalize;
	istream_class->read_fn = fu_stream_input_stream_read;
}

static void
fu_stream_input_stream_init(FuStreamInputStream *self)
{
}

/**
 * fu_stream_input_stream_set_base_stream:
 * @self: a #FuStreamInputStream
 * @base_stream: (transfer none): a #GInputStream
 *
 * Sets the underlying #GInputStream that this stream wraps.
 * The @base_stream is reffed.
 * This is intended for use by subclasses during construction.
 *
 * Since: 2.0.7
 **/
void
fu_stream_input_stream_set_base_stream(FuStreamInputStream *self, GInputStream *base_stream)
{
	FuStreamInputStreamPrivate *priv = fu_stream_input_stream_get_instance_private(self);

	g_return_if_fail(FU_IS_STREAM_INPUT_STREAM(self));
	g_return_if_fail(G_IS_INPUT_STREAM(base_stream));

	g_set_object(&priv->base_stream, base_stream);
}

/**
 * fu_stream_input_stream_from_stream:
 * @stream: (transfer none): a #GInputStream
 *
 * Wraps a #GInputStream and returns a #FuStreamInputStream. The @stream
 * is reffed and future operations on this input stream apply to the
 * underlying #GInputStream.
 *
 * Returns: (transfer full): a #FuStreamInputStream, or %NULL on error
 *
 * Since: 2.0.7
 **/
FuInputStream *
fu_stream_input_stream_from_stream(GInputStream *stream)
{
	g_autoptr(FuStreamInputStream) self = NULL;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);

	self = g_object_new(FU_TYPE_STREAM_INPUT_STREAM, NULL);
	fu_stream_input_stream_set_base_stream(self, stream);

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}
