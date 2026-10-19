/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>

#include "fwupd-error.h"
#include "fwupd-rust-file-input-stream.h"
#include "fwupd-rust-streams.h"
#include "fwupd-unix-input-stream.h"

/**
 * FwupdUnixInputStream:
 *
 * An input stream that can be constructed from a raw Unix FD.
 *
 * Unlike #GUnixInputStream this input stream is seekable.
 *
 * This is private API and is only exported for use by the daemon.
 */
struct _FwupdUnixInputStream {
	FwupdInputStream parent_instance;
	FuRsFileInputStream *rust;
};

G_DEFINE_TYPE(FwupdUnixInputStream, fwupd_unix_input_stream, FWUPD_TYPE_INPUT_STREAM)

static gssize
fwupd_unix_input_stream_read_fn(FwupdInputStream *stream,
				void *buffer,
				gsize count,
				GCancellable *cancellable,
				GError **error)
{
	FwupdUnixInputStream *self = FWUPD_UNIX_INPUT_STREAM(stream);
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
fwupd_unix_input_stream_tell(FwupdInputStream *stream)
{
	FwupdUnixInputStream *self = FWUPD_UNIX_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_tell(self->rust);
}

static gboolean
fwupd_unix_input_stream_can_seek(FwupdInputStream *stream)
{
	return TRUE;
}

static gboolean
fwupd_unix_input_stream_seek(FwupdInputStream *stream,
			     goffset offset,
			     GSeekType type,
			     GCancellable *cancellable,
			     GError **error)
{
	FwupdUnixInputStream *self = FWUPD_UNIX_INPUT_STREAM(stream);

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
 * fwupd_unix_input_stream_new:
 * @fd: a UNIX file descriptor
 * @close_fd: %TRUE to close the file descriptor when done
 * @error: (nullable): optional return location for an error
 *
 * Creates a new seekable input stream for the given file descriptor.
 *
 * NOTE: @fd has to point to a regular file on disk
 *
 * Returns: (transfer full): a #FwupdUnixInputStream
 *
 * Since: 2.2.1
 **/
FwupdUnixInputStream *
fwupd_unix_input_stream_new(gint fd, gboolean close_fd, GError **error)
{
	g_autoptr(FwupdUnixInputStream) self = NULL;
	GStatBuf st = {0};
	g_autofd int autoclose_fd = -1;
	int rust_fd;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* Rust always takes ownership of the fd and closes it on error so we need
	 * some trickery to handle close_fd right for all error cases. */
	if (close_fd)
		autoclose_fd = fd;

	/* check for a regular file -- which doesn't work for apps using a flatpak portal as
	 * FUSE rejects any syscall against inodes coming from a different UID -- including root */
	if (fstat(fd, &st) != 0) {
		if (errno == EACCES) {
			g_debug("ignoring EACCES for fstat");
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to stat fd: %s",
				    fwupd_strerror(errno));
			return NULL;
		}
	} else {
		if (!S_ISREG(st.st_mode)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "fd must be a regular file, got mode 0%o",
				    st.st_mode);
			return NULL;
		}
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

	self = g_object_new(FWUPD_TYPE_UNIX_INPUT_STREAM, NULL);
	self->rust = fu_rs_file_input_stream_new_from_fd(rust_fd, error);
	if (self->rust == NULL)
		return NULL;

	return g_steal_pointer(&self);
}

/**
 * fwupd_unix_input_stream_new_from_filename:
 * @filename: a filename
 * @error: (nullable): optional return location for an error
 *
 * Opens a file and creates a new seekable input stream for it.
 *
 * NOTE: @filename has to be a regular file on disk
 *
 * Returns: (transfer full): a #FwupdInputStream, or %NULL on error
 *
 * Since: 2.2.1
 **/
FwupdUnixInputStream *
fwupd_unix_input_stream_new_from_filename(const gchar *filename, GError **error)
{
	g_autofd gint fd = -1;

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to open %s: %s",
			    filename,
			    fwupd_strerror(errno));
		return NULL;
	}
	return fwupd_unix_input_stream_new(g_steal_fd(&fd), TRUE, error);
}

/**
 * fwupd_unix_input_stream_get_fd:
 * @stream: a #FwupdUnixInputStream
 *
 * Gets the file descriptor backing this stream. The stream retains ownership of the
 * file descriptor, so the caller must not close it.
 *
 * Returns: a file descriptor, or -1 on error
 *
 * Since: 2.2.1
 **/
gint
fwupd_unix_input_stream_get_fd(FwupdUnixInputStream *stream)
{
	g_return_val_if_fail(FWUPD_IS_UNIX_INPUT_STREAM(stream), -1);
	return fu_rs_file_input_stream_get_fd(stream->rust);
}

/**
 * fwupd_unix_input_stream_require_seal:
 * @stream: a #FwupdUnixInputStream
 * @error: (nullable): optional return location for an error
 *
 * Enforces that the file descriptor backing this stream is a memfd with the required seals set.
 *
 * Returns: %TRUE if sealed
 *
 * Since: 2.2.1
 **/
gboolean
fwupd_unix_input_stream_require_seal(FwupdUnixInputStream *stream, GError **error)
{
#ifdef HAVE_MEMFD_CREATE
	gint fd;
	gint seals;

	g_return_val_if_fail(FWUPD_IS_UNIX_INPUT_STREAM(stream), FALSE);
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

static FwupdStreamImpl
fwupd_unix_input_stream_get_stream_impl(FwupdInputStream *stream)
{
	FwupdUnixInputStream *self = FWUPD_UNIX_INPUT_STREAM(stream);
	return fu_rs_file_input_stream_get_stream_impl(self->rust);
}

static void
fwupd_unix_input_stream_finalize(GObject *object)
{
	FwupdUnixInputStream *self = FWUPD_UNIX_INPUT_STREAM(object);
	fu_rs_file_input_stream_free(self->rust);
	G_OBJECT_CLASS(fwupd_unix_input_stream_parent_class)->finalize(object);
}

static void
fwupd_unix_input_stream_class_init(FwupdUnixInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FwupdInputStreamClass *istream_class = FWUPD_INPUT_STREAM_CLASS(klass);
	object_class->finalize = fwupd_unix_input_stream_finalize;
	istream_class->read_fn = fwupd_unix_input_stream_read_fn;
	istream_class->tell = fwupd_unix_input_stream_tell;
	istream_class->can_seek = fwupd_unix_input_stream_can_seek;
	istream_class->seek = fwupd_unix_input_stream_seek;
	istream_class->get_stream_impl = fwupd_unix_input_stream_get_stream_impl;
}

static void
fwupd_unix_input_stream_init(FwupdUnixInputStream *self)
{
}
