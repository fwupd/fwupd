/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUnixSeekableInputStream"

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>

#include "fwupd-error.h"
#include "fwupd-rust-file-input-stream.h"
#include "fwupd-rust-streams.h"

#include "fu-unix-seekable-input-stream.h"

/**
 * FuUnixSeekableInputStream:
 *
 * An input stream that can be constructed from a raw Unix FD.
 */
struct _FuUnixSeekableInputStream {
	FuInputStream parent_instance;
	FuRsFileInputStream *rust;
};

G_DEFINE_TYPE(FuUnixSeekableInputStream, fu_unix_seekable_input_stream, FU_TYPE_INPUT_STREAM)

static gssize
fu_unix_seekable_input_stream_read_fn(FuInputStream *stream,
				      void *buffer,
				      gsize count,
				      GCancellable *cancellable,
				      GError **error)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(stream);
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
fu_unix_seekable_input_stream_tell(FuInputStream *stream)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_tell(self->rust);
}

static gboolean
fu_unix_seekable_input_stream_can_seek(FuInputStream *stream)
{
	return TRUE;
}

static gboolean
fu_unix_seekable_input_stream_seek(FuInputStream *stream,
				   goffset offset,
				   GSeekType type,
				   GCancellable *cancellable,
				   GError **error)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(stream);

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

/**
 * fu_unix_seekable_input_stream_new:
 * @fd: a UNIX file descriptor
 * @close_fd: %TRUE to close the file descriptor when done
 * @error: (nullable): optional return location for an error
 *
 * Creates a new seekable input stream for the given file descriptor.
 *
 * NOTE: @fd has to point to a regular file on disk
 *
 * Returns: (transfer full): a #FuInputStream
 *
 * Since: 2.1.2
 **/
FuInputStream *
fu_unix_seekable_input_stream_new(gint fd, gboolean close_fd, GError **error)
{
	g_autoptr(FuUnixSeekableInputStream) self = NULL;
	GStatBuf st = {0};
	g_autofd int autoclose_fd = -1;
	int rust_fd;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* Rust always takes ownership of the fd and closes it on error so we need
	 * some trickery to handle close_fd right for all error cases. */
	if (close_fd)
		autoclose_fd = fd;

	/* check for a regular file */
	if (fstat(fd, &st) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to stat fd: %s",
			    fwupd_strerror(errno));
		return NULL;
	}
	if (!S_ISREG(st.st_mode)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "fd must be a regular file, got mode 0%o",
			    st.st_mode);
		return NULL;
	}

	if (close_fd) {
		rust_fd = g_steal_fd(&autoclose_fd);
	} else {
		rust_fd = dup(fd);
		if (rust_fd < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to dup() the file descriptor: %s",
				    fwupd_strerror(errno));
			return NULL;
		}
	}

	self = g_object_new(FU_TYPE_UNIX_SEEKABLE_INPUT_STREAM, NULL);
	self->rust = fu_rs_file_input_stream_new_from_fd(rust_fd, error);
	if (self->rust == NULL)
		return NULL;

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}

/**
 * fu_unix_seekable_input_stream_require_seal:
 * @stream: a #FuUnixSeekableInputStream
 * @error: (nullable): optional return location for an error
 *
 * Enforces that the file descriptor backing this stream is a memfd with the required seals set.
 *
 * Returns: %TRUE if sealed
 *
 * Since: 2.1.7
 **/
gboolean
fu_unix_seekable_input_stream_require_seal(FuUnixSeekableInputStream *stream, GError **error)
{
#ifdef HAVE_MEMFD_CREATE
	gint fd;
	gint seals;

	g_return_val_if_fail(FU_IS_UNIX_SEEKABLE_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fd = fu_rs_file_input_stream_get_fd(stream->rust);
	seals = fcntl(fd, F_GET_SEALS);
	if (seals < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "F_GET_SEALS not supported: %s",
			    fwupd_strerror(errno));
		return FALSE;
	}
	if ((seals & F_SEAL_SEAL) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "fd not sealed");
		return FALSE;
	}
	if ((seals & F_SEAL_WRITE) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no WRITE seal");
		return FALSE;
	}
	if ((seals & F_SEAL_SHRINK) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no SHRINK seal");
		return FALSE;
	}
	if ((seals & F_SEAL_GROW) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no GROW seal");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "F_GET_SEALS not supported");
	return FALSE;
#endif
}

static FuRsStreamImpl *
fu_unix_seekable_input_stream_get_stream_impl(FuInputStream *stream)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_get_stream_impl(self->rust);
}

static void
fu_unix_seekable_input_stream_finalize(GObject *object)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(object);
	fu_rs_file_input_stream_free(self->rust);
	G_OBJECT_CLASS(fu_unix_seekable_input_stream_parent_class)->finalize(object);
}

static void
fu_unix_seekable_input_stream_class_init(FuUnixSeekableInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuInputStreamClass *istream_class = FU_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fu_unix_seekable_input_stream_finalize;
	istream_class->read_fn = fu_unix_seekable_input_stream_read_fn;
	istream_class->tell = fu_unix_seekable_input_stream_tell;
	istream_class->can_seek = fu_unix_seekable_input_stream_can_seek;
	istream_class->seek = fu_unix_seekable_input_stream_seek;
	istream_class->get_stream_impl = fu_unix_seekable_input_stream_get_stream_impl;
}

static void
fu_unix_seekable_input_stream_init(FuUnixSeekableInputStream *self)
{
}
