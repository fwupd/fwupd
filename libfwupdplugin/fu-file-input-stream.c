/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFileInputStream"

#include "config.h"

#include "fwupd-rust-file-input-stream.h"

#include "fu-file-input-stream.h"

/**
 * FuFileInputStream:
 *
 * An input stream that replaces #GFileInputStream, providing file-specific
 * operations within the #FuInputStream type hierarchy. This implementation
 * is a drop-in replacement for #GFileInputStream for the needs
 * within fwupd.
 */

struct _FuFileInputStream {
	FuInputStream parent_instance;
	FuRsFileInputStream *rust;
};

G_DEFINE_TYPE(FuFileInputStream, fu_file_input_stream, FU_TYPE_INPUT_STREAM)

static gssize
fu_file_input_stream_read_fn(FuInputStream *stream,
			     void *buffer,
			     gsize count,
			     GCancellable *cancellable,
			     GError **error)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(stream);
	gssize rc;

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return -1;

	rc = fu_rs_file_input_stream_read(self->rust, buffer, count);
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
fu_file_input_stream_tell(FuInputStream *stream)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_tell(self->rust);
}

static gboolean
fu_file_input_stream_can_seek(FuInputStream *stream)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_can_seek(self->rust);
}

static gboolean
fu_file_input_stream_seek(FuInputStream *stream,
			  goffset offset,
			  GSeekType type,
			  GCancellable *cancellable,
			  GError **error)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(stream);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;

	if (!fu_rs_file_input_stream_seek(self->rust, offset, (gint32)type)) {
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
fu_file_input_stream_finalize(GObject *object)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(object);
	fu_rs_file_input_stream_free(self->rust);
	G_OBJECT_CLASS(fu_file_input_stream_parent_class)->finalize(object);
}

static FuRsStreamImpl *
fu_file_input_stream_get_stream_impl(FuInputStream *stream)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_get_stream_impl(self->rust);
}

static void
fu_file_input_stream_class_init(FuFileInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuInputStreamClass *istream_class = FU_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fu_file_input_stream_finalize;
	istream_class->read_fn = fu_file_input_stream_read_fn;
	istream_class->tell = fu_file_input_stream_tell;
	istream_class->can_seek = fu_file_input_stream_can_seek;
	istream_class->seek = fu_file_input_stream_seek;
	istream_class->get_stream_impl = fu_file_input_stream_get_stream_impl;
}

static void
fu_file_input_stream_init(FuFileInputStream *self)
{
	self->rust = NULL;
}

/**
 * fu_file_input_stream_from_file:
 * @file: a #GFile
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Opens a #GFile for reading and returns a #FuFileInputStream. Use
 * this instead of g_file_read().
 *
 * Returns: (transfer full): a #FuFileInputStream, or %NULL on error
 *
 * Since: 2.1.7
 **/
FuFileInputStream *
fu_file_input_stream_from_file(GFile *file, GCancellable *cancellable, GError **error)
{
	g_autoptr(FuFileInputStream) self = NULL;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail(G_IS_FILE(file), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return NULL;

	path = g_file_get_path(file);
	if (path == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "cannot get path from GFile"); /* nocheck:error */
		fwupd_error_convert(error);
		return NULL;
	}

	self = g_object_new(FU_TYPE_FILE_INPUT_STREAM, NULL);
	self->rust = fu_rs_file_input_stream_new_from_path(path, error);
	if (self->rust == NULL)
		return NULL;

	return g_steal_pointer(&self);
}

/**
 * fu_file_input_stream_get_file_size:
 * @stream: a #FuFileInputStream
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Return the size of the file, in bytes. Returns zero if an error
 * occurs, a caller that needs to distinguish between zero-sized
 * files and zero-means-error must provide a GError.
 *
 * Returns: The file size in bytes or zero on error
 *
 * Since: 2.1.7
 **/
guint64
fu_file_input_stream_get_file_size(FuFileInputStream *stream,
				   GCancellable *cancellable,
				   GError **error)
{
	g_autoptr(GFileInfo) info = NULL;

	g_return_val_if_fail(FU_IS_FILE_INPUT_STREAM(stream), 0);
	g_return_val_if_fail(error == NULL || *error == NULL, 0);

	info = g_file_input_stream_query_info(/* nocheck:blocked */
					      stream->file_stream,
					      G_FILE_ATTRIBUTE_STANDARD_SIZE,
					      cancellable,
					      error);
	if (info == NULL) {
		fwupd_error_convert(error);
		return 0;
	}

	return g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
}
