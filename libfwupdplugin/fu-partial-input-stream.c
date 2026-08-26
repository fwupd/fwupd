/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPartialInputStream"

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-rust-partial-input-stream.h"

#include "fu-common.h"
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
	FuInputStream parent_instance;
	FuInputStream *base_stream; /* GObject ref to keep alive */
	FuRsPartialInputStream *rust;
};

static void
fu_partial_input_stream_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuPartialInputStream,
			fu_partial_input_stream,
			FU_TYPE_INPUT_STREAM,
			G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
					      fu_partial_input_stream_codec_iface_init))

static void
fu_partial_input_stream_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(codec);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "Offset",
				      fu_rs_partial_input_stream_offset(self->rust));
	fwupd_codec_string_append_hex(str,
				      idt,
				      "Size",
				      fu_rs_partial_input_stream_size(self->rust));
}

static void
fu_partial_input_stream_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fu_partial_input_stream_add_string;
}

static goffset
fu_partial_input_stream_tell(FuInputStream *stream)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(stream);
	return fu_rs_partial_input_stream_tell(self->rust);
}

static gboolean
fu_partial_input_stream_can_seek(FuInputStream *stream)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(stream);
	return fu_rs_partial_input_stream_can_seek(self->rust);
}

static gboolean
fu_partial_input_stream_seek(FuInputStream *stream,
			     goffset offset,
			     GSeekType type,
			     GCancellable *cancellable,
			     GError **error)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(stream);

	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_rs_partial_input_stream_seek(self->rust, offset, (gint32)type)) {
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

/**
 * fu_partial_input_stream_new:
 * @stream: a base #FuInputStream
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
FuInputStream *
fu_partial_input_stream_new(FuInputStream *stream, gsize offset, gsize size, GError **error)
{
	gsize base_sz = 0;
	g_autoptr(FuPartialInputStream) self = g_object_new(FU_TYPE_PARTIAL_INPUT_STREAM, NULL);
	FuRsStreamImpl *impl;

	g_return_val_if_fail(FU_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	self->base_stream = g_object_ref(stream);

	/* sanity check */
	if (!fu_input_stream_size(stream, &base_sz, error)) {
		g_prefix_error_literal(error, "failed to get size: ");
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
		size = base_sz - offset;
	} else {
		if (fu_size_checked_add(offset, size) > base_sz) {
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
	}

	impl = fu_input_stream_get_stream_impl(stream);
	if (impl == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Partial streams only support Rust-based streams");
		return NULL;
	}
	self->rust = fu_rs_partial_input_stream_new(impl, offset, size);

	/* success */
	return FU_INPUT_STREAM(g_steal_pointer(&self));
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
	return fu_rs_partial_input_stream_offset(self->rust);
}

/**
 * fu_partial_input_stream_get_size:
 * @self: a #FuPartialInputStream
 *
 * Gets the size of the stream.
 *
 * Returns: integer
 *
 * Since: 2.0.0
 **/
gsize
fu_partial_input_stream_get_size(FuPartialInputStream *self)
{
	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), G_MAXSIZE);
	return fu_rs_partial_input_stream_size(self->rust);
}

static gssize
fu_partial_input_stream_read_fn(FuInputStream *stream,
				void *buffer,
				gsize count,
				GCancellable *cancellable,
				GError **error)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(stream);
	gssize rc;
	g_return_val_if_fail(FU_IS_PARTIAL_INPUT_STREAM(self), -1);
	g_return_val_if_fail(error == NULL || *error == NULL, -1);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return -1;

	rc = fu_rs_partial_input_stream_read(self->rust, buffer, count);
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

static void
fu_partial_input_stream_finalize(GObject *object)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(object);
	/* must free the Rust stream first */
	fu_rs_partial_input_stream_free(self->rust);
	if (self->base_stream != NULL)
		g_object_unref(self->base_stream);
	G_OBJECT_CLASS(fu_partial_input_stream_parent_class)->finalize(object);
}

static FuRsStreamImpl *
fu_partial_input_stream_get_stream_impl(FuInputStream *stream)
{
	FuPartialInputStream *self = FU_PARTIAL_INPUT_STREAM(stream);
	return fu_rs_partial_input_stream_get_stream_impl(self->rust);
}

static void
fu_partial_input_stream_class_init(FuPartialInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuInputStreamClass *istream_class = FU_INPUT_STREAM_CLASS(klass);
	istream_class->read_fn = fu_partial_input_stream_read_fn;
	istream_class->tell = fu_partial_input_stream_tell;
	istream_class->can_seek = fu_partial_input_stream_can_seek;
	istream_class->seek = fu_partial_input_stream_seek;
	istream_class->get_stream_impl = fu_partial_input_stream_get_stream_impl;
	object_class->finalize = fu_partial_input_stream_finalize;
}

static void
fu_partial_input_stream_init(FuPartialInputStream *self)
{
}
