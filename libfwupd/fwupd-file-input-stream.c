/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-file-input-stream.h"
#include "fwupd-rust-file-input-stream.h"

/**
 * FwupdFileInputStream:
 *
 * An input stream that replaces #GFileInputStream, providing file-specific
 * operations within the #FwupdInputStream type hierarchy. This implementation
 * is a drop-in replacement for #GFileInputStream for the needs
 * within fwupd.
 *
 * This is private API and is only exported for use by libfwupd.
 */

struct _FwupdFileInputStream {
	FwupdInputStream parent_instance;
	FuRsFileInputStream *rust;
};

G_DEFINE_TYPE(FwupdFileInputStream, fwupd_file_input_stream, FWUPD_TYPE_INPUT_STREAM)

static gssize
fwupd_file_input_stream_read_fn(FwupdInputStream *stream,
				void *buffer,
				gsize count,
				GCancellable *cancellable,
				GError **error)
{
	FwupdFileInputStream *self = FWUPD_FILE_INPUT_STREAM(stream);
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
fwupd_file_input_stream_tell(FwupdInputStream *stream)
{
	FwupdFileInputStream *self = FWUPD_FILE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_tell(self->rust);
}

static gboolean
fwupd_file_input_stream_can_seek(FwupdInputStream *stream)
{
	FwupdFileInputStream *self = FWUPD_FILE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_can_seek(self->rust);
}

static gboolean
fwupd_file_input_stream_seek(FwupdInputStream *stream,
			     goffset offset,
			     GSeekType type,
			     GCancellable *cancellable,
			     GError **error)
{
	FwupdFileInputStream *self = FWUPD_FILE_INPUT_STREAM(stream);

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
fwupd_file_input_stream_finalize(GObject *object)
{
	FwupdFileInputStream *self = FWUPD_FILE_INPUT_STREAM(object);
	fu_rs_file_input_stream_free(self->rust);
	G_OBJECT_CLASS(fwupd_file_input_stream_parent_class)->finalize(object);
}

static FwupdInputStreamImpl
fwupd_file_input_stream_get_impl(FwupdInputStream *stream)
{
	FwupdFileInputStream *self = FWUPD_FILE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_get_impl(self->rust);
}

static void
fwupd_file_input_stream_class_init(FwupdFileInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FwupdInputStreamClass *istream_class = FWUPD_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fwupd_file_input_stream_finalize;
	istream_class->read_fn = fwupd_file_input_stream_read_fn;
	istream_class->tell = fwupd_file_input_stream_tell;
	istream_class->can_seek = fwupd_file_input_stream_can_seek;
	istream_class->seek = fwupd_file_input_stream_seek;
	istream_class->get_impl = fwupd_file_input_stream_get_impl;
}

static void
fwupd_file_input_stream_init(FwupdFileInputStream *self)
{
}

/**
 * fwupd_file_input_stream_new:
 * @path: a filename
 * @error: (nullable): optional return location for an error
 *
 * Opens the file as an input stream.
 *
 * Returns: (transfer full): a #FwupdFileInputStream, or %NULL on error
 *
 * Since: 2.2.1
 **/
FwupdFileInputStream *
fwupd_file_input_stream_new(const gchar *path, GError **error)
{
	g_autoptr(FwupdFileInputStream) self = NULL;

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	self = g_object_new(FWUPD_TYPE_FILE_INPUT_STREAM, NULL);
	self->rust = fu_rs_file_input_stream_new_from_path(path, error);
	if (self->rust == NULL) {
		fwupd_error_convert(error);
		return NULL;
	}
	return g_steal_pointer(&self);
}

/**
 * fwupd_file_input_stream_new_from_file:
 * @file: a #GFile
 * @error: (nullable): optional return location for an error
 *
 * Opens a #GFile for reading and returns a #FwupdFileInputStream. Use
 * this instead of g_file_read().
 *
 * Returns: (transfer full): a #FwupdFileInputStream, or %NULL on error
 *
 * Since: 2.2.1
 **/
FwupdFileInputStream *
fwupd_file_input_stream_new_from_file(GFile *file, GError **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail(G_IS_FILE(file), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	path = g_file_get_path(file);
	if (path == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "cannot get path from GFile"); /* nocheck:error */
		fwupd_error_convert(error);
		return NULL;
	}
	return fwupd_file_input_stream_new(path, error);
}
