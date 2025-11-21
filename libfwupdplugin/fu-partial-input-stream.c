/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPartialInputStream"

#include "config.h"

#include "fwupd-codec.h"

#include "fu-input-stream.h"
#include "fu-partial-input-stream-private.h"

/**
 * FuPartialInputStream:
 *
 * A input stream that is a slice of another input stream.
 *
 *       off    sz
 *    [xxxxxxxxxxxx]
 *       |  0x6  |
 *        \      \
 *         \      \
 *          \      |
 *           |     |
 *          [xxxxxx]
 *
 * xxx offset: 2, sz: 6
 */

struct _FuPartialInputStream {
	GInputStream parent_instance;
	GInputStream *base_stream;
	gsize offset;
	gsize size;
};

static void
fu_partial_input_stream_seekable_iface_init(GSeekableIface *iface);
static void
fu_partial_input_stream_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuPartialInputStream,
			fu_partial_input_stream,
			G_TYPE_INPUT_STREAM,
			G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
					      fu_partial_input_stream_seekable_iface_init)
			    G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						  fu_partial_input_stream_codec_iface_init))

static void
fu_partial_input_stream_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(codec);
	fwupd_codec_string_append_hex(str, idt, "Offset", self->offset);
	fwupd_codec_string_append_hex(str, idt, "Size", self->size);
}

static void
fu_partial_input_stream_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fu_partial_input_stream_add_string;
}

static goffset
fu_partial_input_stream_tell(GSeekable *seekable)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(seekable);
	return g_seekable_tell(G_SEEKABLE(self->base_stream)) - self->offset;
}

static gboolean
fu_partial_input_stream_can_seek(GSeekable *seekable)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(seekable);
	return g_seekable_can_seek(G_SEEKABLE(self->base_stream));
}

static gboolean
fu_partial_input_stream_seek(GSeekable *seekable,
			     goffset offset,
			     GSeekType type,
			     GCancellable *cancellable,
			     GError **error)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(seekable);

	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (type == G_SEEK_CUR) {
		goffset pos = g_seekable_tell(G_SEEKABLE(self->base_stream));
		return g_seekable_seek(G_SEEKABLE(self->base_stream),
				       self->offset + pos + offset,
				       G_SEEK_SET,
				       cancellable,
				       error);
	}
	if (type == G_SEEK_END) {
		return g_seekable_seek(G_SEEKABLE(self->base_stream),
				       self->offset + self->size + offset,
				       G_SEEK_SET,
				       cancellable,
				       error);
	}
	return g_seekable_seek(G_SEEKABLE(self->base_stream),
			       self->offset + offset,
			       G_SEEK_SET,
			       cancellable,
			       error);
}

static gboolean
fu_partial_input_stream_can_truncate(GSeekable *seekable)
{
	return FALSE;
}

static gboolean
fu_partial_input_stream_truncate(GSeekable *seekable,
				 goffset offset,
				 GCancellable *cancellable,
				 GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot truncate FuPartialInputStream");
	return FALSE;
}

static void
fu_partial_input_stream_seekable_iface_init(GSeekableIface *iface)
{
	iface->tell = fu_partial_input_stream_tell;
	iface->can_seek = fu_partial_input_stream_can_seek;
	iface->seek = fu_partial_input_stream_seek;
	iface->can_truncate = fu_partial_input_stream_can_truncate;
	iface->truncate_fn = fu_partial_input_stream_truncate;
}

/**
 * fu_partial_input_stream_new:
 * @stream: a base #GInputStream
 * @offset: offset into @stream
 * @size: size of @stream in bytes, or %G_MAXSIZE for the "rest" of the stream
 * @error: (nullable): optional return location for an error
 *
 * Creates a partial input stream where content is read from the donor stream.
 *
 * Returns: (transfer full): a #FuPartialInputStream, or %NULL on error
 *
 * Since: 2.0.0
 **/
GInputStream *
fu_partial_input_stream_new(GInputStream *stream, gsize offset, gsize size, GError **error)
{
	gsize base_sz = 0;
	g_autoptr(FuPartialInputStream) self = g_object_new(FU_TYPE_PARTIAL_INPUT_STREAM, NULL);

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	self->base_stream = g_object_ref(stream);
	self->offset = offset;

	/* sanity check */
	if (!fu_input_stream_size(stream, &base_sz, error)) {
		g_prefix_error(error, "failed to get size: ");
		return NULL;
	}
	if (size == G_MAXSIZE) {
		if (offset > base_sz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "base stream was 0x%x bytes in size "
				    "and tried to create partial stream @0x%x",
				    (guint)base_sz,
				    (guint)offset);
			return NULL;
		}
		self->size = base_sz - offset;
	} else {
		if (offset + size > base_sz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "base stream was 0x%x bytes in size, and tried to create "
				    "partial stream @0x%x of 0x%x bytes",
				    (guint)base_sz,
				    (guint)offset,
				    (guint)size);
			return NULL;
		}
		self->size = size;
	}

	/* success */
	return G_INPUT_STREAM(g_steal_pointer(&self));
}

/**
 * fu_partial_input_stream_get_offset:
 * @self: a #FuPartialInputStream
 *
 * Gets the offset of the stream.
 *
 * Returns: integer
 *
 * Since: 2.0.0
 **/
gsize
fu_partial_input_stream_get_offset(FuPartialInputStream *self)
{
	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), G_MAXSIZE);
	return self->offset;
}

/**
 * fu_partial_input_stream_get_size:
 * @self: a #FuPartialInputStream
 *
 * Gets the offset of the stream.
 *
 * Returns: integer
 *
 * Since: 2.0.0
 **/
gsize
fu_partial_input_stream_get_size(FuPartialInputStream *self)
{
	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), G_MAXSIZE);
	return self->size;
}

static gssize
fu_partial_input_stream_read(GInputStream *stream,
			     void *buffer,
			     gsize count,
			     GCancellable *cancellable,
			     GError **error)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(stream);
	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), -1);
	g_return_val_if_fail(error == NULL || *error == NULL, -1);
	if (self->size < (gsize)g_seekable_tell(G_SEEKABLE(stream))) {
		g_warning("base stream is outside seekable range");
		return 0;
	}
	count = MIN(count, self->size - g_seekable_tell(G_SEEKABLE(stream)));
	return g_input_stream_read(self->base_stream, buffer, count, cancellable, error);
}

static void
fu_partial_input_stream_finalize(GObject *object)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(object);
	if (self->base_stream != NULL)
		g_object_unref(self->base_stream);
	G_OBJECT_CLASS(fu_partial_input_stream_parent_class)->finalize(object);
}

static void
fu_partial_input_stream_class_init(FuPartialInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GInputStreamClass *istream_class = G_INPUT_STREAM_CLASS(klass);
	istream_class->read_fn = fu_partial_input_stream_read;
	object_class->finalize = fu_partial_input_stream_finalize;
}

static void
fu_partial_input_stream_init(FuPartialInputStream *self)
{
}
