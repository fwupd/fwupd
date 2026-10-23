/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-input-stream-private.h"

/**
 * FwupdInputStream:
 *
 * An abstract seekable input stream, implemented by subclasses in libfwupd.
 *
 * This is private API and is only exported for use by libfwupd.
 */

static void
fwupd_input_stream_seekable_iface_init(GSeekableIface *iface);

G_DEFINE_TYPE_WITH_CODE(FwupdInputStream,
			fwupd_input_stream,
			G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
					      fwupd_input_stream_seekable_iface_init))

static goffset
fwupd_input_stream_default_tell(FwupdInputStream *stream)
{
	return 0;
}

static gboolean
fwupd_input_stream_default_can_seek(FwupdInputStream *stream)
{
	return FALSE;
}

static gboolean
fwupd_input_stream_default_seek(FwupdInputStream *stream,
				goffset offset,
				GSeekType type,
				GCancellable *cancellable,
				GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "stream is not seekable");
	return FALSE;
}

static goffset
fwupd_input_stream_seekable_tell(GSeekable *seekable)
{
	FwupdInputStream *self = FWUPD_INPUT_STREAM(seekable);
	FwupdInputStreamClass *klass = FWUPD_INPUT_STREAM_GET_CLASS(self);
	return klass->tell(self);
}

static gboolean
fwupd_input_stream_seekable_can_seek(GSeekable *seekable)
{
	FwupdInputStream *self = FWUPD_INPUT_STREAM(seekable);
	FwupdInputStreamClass *klass = FWUPD_INPUT_STREAM_GET_CLASS(self);
	return klass->can_seek(self);
}

static gboolean
fwupd_input_stream_seekable_seek(GSeekable *seekable,
				 goffset offset,
				 GSeekType type,
				 GCancellable *cancellable,
				 GError **error)
{
	FwupdInputStream *self = FWUPD_INPUT_STREAM(seekable);
	FwupdInputStreamClass *klass = FWUPD_INPUT_STREAM_GET_CLASS(self);
	return klass->seek(self, offset, type, cancellable, error);
}

static gboolean
fwupd_input_stream_seekable_can_truncate(GSeekable *seekable)
{
	return FALSE;
}

static gboolean
fwupd_input_stream_seekable_truncate(GSeekable *seekable,
				     goffset offset,
				     GCancellable *cancellable,
				     GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot truncate FwupdInputStream");
	return FALSE;
}

static void
fwupd_input_stream_seekable_iface_init(GSeekableIface *iface)
{
	iface->tell = fwupd_input_stream_seekable_tell;
	iface->can_seek = fwupd_input_stream_seekable_can_seek;
	iface->seek = fwupd_input_stream_seekable_seek;
	iface->can_truncate = fwupd_input_stream_seekable_can_truncate;
	iface->truncate_fn = fwupd_input_stream_seekable_truncate;
}

static void
fwupd_input_stream_constructed(GObject *object)
{
	FwupdInputStreamClass *klass = FWUPD_INPUT_STREAM_GET_CLASS(object);

	G_OBJECT_CLASS(fwupd_input_stream_parent_class)->constructed(object);

	/* every FwupdInputStream subclass must implement read_fn */
	g_assert(klass->read_fn != NULL); /* nocheck:blocked */
}

static void
fwupd_input_stream_class_init(FwupdInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fwupd_input_stream_constructed;
	klass->tell = fwupd_input_stream_default_tell;
	klass->can_seek = fwupd_input_stream_default_can_seek;
	klass->seek = fwupd_input_stream_default_seek;
}

static void
fwupd_input_stream_init(FwupdInputStream *self)
{
}

/**
 * fwupd_input_stream_read:
 * @stream: a #FwupdInputStream
 * @buffer: (not nullable): a buffer to read data into
 * @count: the number of bytes that will be read from the stream
 * @cancellable: (nullable): optional #GCancellable object
 * @error: (nullable): optional return location for an error
 *
 * Tries to read @count bytes from the stream into the buffer starting at @buffer.
 *
 * Returns: number of bytes read, 0 for end of stream, or -1 on error
 *
 * Since: 2.2.1
 **/
gssize
fwupd_input_stream_read(FwupdInputStream *stream,
			void *buffer,
			gsize count,
			GCancellable *cancellable,
			GError **error)
{
	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), -1);
	g_return_val_if_fail(buffer != NULL, -1);
	g_return_val_if_fail(error == NULL || *error == NULL, -1);
	return FWUPD_INPUT_STREAM_GET_CLASS(stream)->read_fn(stream,
							     buffer,
							     count,
							     cancellable,
							     error);
}

/**
 * fwupd_input_stream_read_all:
 * @stream: a #FwupdInputStream
 * @buffer: (not nullable): a buffer to read data into
 * @count: the number of bytes that will be read from the stream
 * @bytes_read: (out) (nullable): location to store the number of bytes that was read
 * @cancellable: (nullable): optional #GCancellable object
 * @error: (nullable): optional return location for an error
 *
 * Tries to read @count bytes from the stream into the buffer starting at @buffer.
 * Will block during this read.
 *
 * This function is similar to fwupd_input_stream_read(), except it tries to read as many bytes as
 * requested, only stopping on an error or end of stream.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.2.1
 **/
gboolean
fwupd_input_stream_read_all(FwupdInputStream *stream,
			    void *buffer,
			    gsize count,
			    gsize *bytes_read,
			    GCancellable *cancellable,
			    GError **error)
{
	gsize total = 0;
	guint8 *buf = buffer;

	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(buffer != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	while (total < count) {
		gssize rc;

		if (cancellable && g_cancellable_set_error_if_cancelled(cancellable, error)) {
			if (bytes_read != NULL)
				*bytes_read = total;
			return FALSE;
		}

		rc =
		    fwupd_input_stream_read(stream, buf + total, count - total, cancellable, error);
		if (rc == -1) {
			if (bytes_read != NULL)
				*bytes_read = total;
			return FALSE;
		}
		if (rc == 0)
			break;
		total += rc;
	}
	if (bytes_read != NULL)
		*bytes_read = total;
	return TRUE;
}

/**
 * fwupd_input_stream_get_impl: (skip):
 * @stream: a #FwupdInputStream
 *
 * Returns the Rust-side stream implementation handle for the given stream.
 *
 * Returns: (transfer full): an opaque #FwupdInputStreamImpl, free with fu_stream_impl_free()
 *
 * Since: 2.2.1
 **/
FwupdInputStreamImpl
fwupd_input_stream_get_impl(FwupdInputStream *stream)
{
	FwupdInputStreamClass *klass;

	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), NULL);

	klass = FWUPD_INPUT_STREAM_GET_CLASS(stream);
	g_return_val_if_fail(klass->get_impl != NULL, NULL);

	return klass->get_impl(stream);
}
