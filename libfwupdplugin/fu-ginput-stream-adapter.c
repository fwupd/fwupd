/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuGInputStreamAdapter"

#include "config.h"

#include "fu-input-stream.h"

/**
 * FuGInputStreamAdapter:
 *
 * A #GInputStream subclass that wraps an #FuInputStream, bridging
 * fwupd's stream abstraction back to the GLib #GInputStream type
 * hierarchy for use with external APIs.
 */

typedef struct {
	GInputStream parent_instance; /* nocheck:blocked */
	FuInputStream *fu_stream;
} FuGInputStreamAdapter;

typedef struct {
	GInputStreamClass parent_class; /* nocheck:blocked */
} FuGInputStreamAdapterClass;

GType
fu_ginput_stream_adapter_get_type(void);

static void
fu_ginput_stream_adapter_seekable_iface_init(GSeekableIface *iface);

G_DEFINE_TYPE_WITH_CODE(FuGInputStreamAdapter,
			fu_ginput_stream_adapter,
			G_TYPE_INPUT_STREAM, /* nocheck:blocked */
			G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
					      fu_ginput_stream_adapter_seekable_iface_init))

static gssize
fu_ginput_stream_adapter_read_fn(GInputStream *stream, /* nocheck:blocked */
				 void *buffer,
				 gsize count,
				 GCancellable *cancellable,
				 GError **error)
{
	FuGInputStreamAdapter *self = (FuGInputStreamAdapter *)stream;
	return fu_input_stream_read(self->fu_stream, buffer, count, cancellable, error);
}

static goffset
fu_ginput_stream_adapter_tell(GSeekable *seekable)
{
	FuGInputStreamAdapter *self = (FuGInputStreamAdapter *)seekable;
	return g_seekable_tell(G_SEEKABLE(self->fu_stream));
}

static gboolean
fu_ginput_stream_adapter_can_seek(GSeekable *seekable)
{
	FuGInputStreamAdapter *self = (FuGInputStreamAdapter *)seekable;
	return g_seekable_can_seek(G_SEEKABLE(self->fu_stream));
}

static gboolean
fu_ginput_stream_adapter_seek(GSeekable *seekable,
			      goffset offset,
			      GSeekType type,
			      GCancellable *cancellable,
			      GError **error)
{
	FuGInputStreamAdapter *self = (FuGInputStreamAdapter *)seekable;
	return g_seekable_seek(G_SEEKABLE(self->fu_stream), offset, type, cancellable, error);
}

static gboolean
fu_ginput_stream_adapter_can_truncate(GSeekable *seekable)
{
	return FALSE;
}

static gboolean
fu_ginput_stream_adapter_truncate(GSeekable *seekable,
				  goffset offset,
				  GCancellable *cancellable,
				  GError **error)
{
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "cannot truncate");
	return FALSE;
}

static void
fu_ginput_stream_adapter_seekable_iface_init(GSeekableIface *iface)
{
	iface->tell = fu_ginput_stream_adapter_tell;
	iface->can_seek = fu_ginput_stream_adapter_can_seek;
	iface->seek = fu_ginput_stream_adapter_seek;
	iface->can_truncate = fu_ginput_stream_adapter_can_truncate;
	iface->truncate_fn = fu_ginput_stream_adapter_truncate;
}

static void
fu_ginput_stream_adapter_finalize(GObject *object)
{
	FuGInputStreamAdapter *self = (FuGInputStreamAdapter *)object;
	g_clear_object(&self->fu_stream);
	G_OBJECT_CLASS(fu_ginput_stream_adapter_parent_class)->finalize(object);
}

static void
fu_ginput_stream_adapter_class_init(FuGInputStreamAdapterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GInputStreamClass *istream_class = G_INPUT_STREAM_CLASS(klass); /* nocheck:blocked */
	object_class->finalize = fu_ginput_stream_adapter_finalize;
	istream_class->read_fn = fu_ginput_stream_adapter_read_fn;
}

static void
fu_ginput_stream_adapter_init(FuGInputStreamAdapter *self)
{
}

/**
 * fu_input_stream_as_g_input_stream:
 * @stream: a #FuInputStream
 *
 * Creates a #GInputStream adapter that delegates all reads and seeks to @stream.
 * Use this when a GLib or external API requires a #GInputStream.
 *
 * Returns: (transfer full): a #GInputStream
 *
 * Since: 2.1.7
 **/
GInputStream *
fu_input_stream_as_g_input_stream(FuInputStream *stream) /* nocheck:name */
{
	FuGInputStreamAdapter *self;

	g_return_val_if_fail(FU_IS_INPUT_STREAM(stream), NULL);

	self = g_object_new(fu_ginput_stream_adapter_get_type(), NULL);
	self->fu_stream = g_object_ref(stream);

	return G_INPUT_STREAM(self); /* nocheck:blocked */
}
