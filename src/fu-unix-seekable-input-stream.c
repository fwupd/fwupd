/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUnixSeekableInputStream"

#include "config.h"

#include <fcntl.h>
#include <gio/gunixinputstream.h>
#include <glib/gstdio.h>

#include "fwupd-error.h"

#include "fu-stream-input-stream-private.h"
#include "fu-unix-seekable-input-stream.h"

struct _FuUnixSeekableInputStream {
	FuStreamInputStream parent_instance;
	gint fd;
};

static void
fu_unix_seekable_input_stream_seekable_iface_init(GSeekableIface *iface);

/* see https://gitlab.gnome.org/GNOME/glib/-/issues/3200 for why this is needed */
G_DEFINE_TYPE_WITH_CODE(FuUnixSeekableInputStream,
			fu_unix_seekable_input_stream,
			FU_TYPE_STREAM_INPUT_STREAM,
			G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
					      fu_unix_seekable_input_stream_seekable_iface_init))

static goffset
fu_unix_seekable_input_stream_tell(GSeekable *seekable)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(seekable);
	goffset rc = lseek(self->fd, 0, SEEK_CUR);
	if (rc < 0)
		g_critical("cannot tell FuUnixSeekableInputStream: %s", fwupd_strerror(errno));
	return rc;
}

static gboolean
fu_unix_seekable_input_stream_can_seek(GSeekable *seekable)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(seekable);
	return lseek(self->fd, 0, SEEK_CUR) >= 0;
}

/* from glocalfileinputstream.c */
static int
fu_unix_seekable_input_stream_seek_type_to_lseek(GSeekType type)
{
	switch (type) {
	default:
	case G_SEEK_CUR:
		return SEEK_CUR;
	case G_SEEK_SET:
		return SEEK_SET;
	case G_SEEK_END:
		return SEEK_END;
	}
}

static gboolean
fu_unix_seekable_input_stream_seek(GSeekable *seekable,
				   goffset offset,
				   GSeekType type,
				   GCancellable *cancellable,
				   GError **error)
{
	FuUnixSeekableInputStream *self = FU_UNIX_SEEKABLE_INPUT_STREAM(seekable);
	goffset rc;

	g_return_val_if_fail(FU_IS_UNIX_SEEKABLE_INPUT_STREAM(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	rc = lseek(self->fd, offset, fu_unix_seekable_input_stream_seek_type_to_lseek(type));
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
			    g_io_error_from_errno(errno),
			    "Error seeking file descriptor: %s",
			    fwupd_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_unix_seekable_input_stream_can_truncate(GSeekable *seekable)
{
	return FALSE;
}

static gboolean
fu_unix_seekable_input_stream_truncate(GSeekable *seekable,
				       goffset offset,
				       GCancellable *cancellable,
				       GError **error)
{
	/* using GIOError here as this will eventually go into GLib */
	g_set_error_literal(error,
			    G_IO_ERROR,		      /* nocheck:error */
			    G_IO_ERROR_NOT_SUPPORTED, /* nocheck:error */
			    "cannot truncate FuUnixSeekableInputStream");
	return FALSE;
}

static void
fu_unix_seekable_input_stream_seekable_iface_init(GSeekableIface *iface)
{
	iface->tell = fu_unix_seekable_input_stream_tell;
	iface->can_seek = fu_unix_seekable_input_stream_can_seek;
	iface->seek = fu_unix_seekable_input_stream_seek;
	iface->can_truncate = fu_unix_seekable_input_stream_can_truncate;
	iface->truncate_fn = fu_unix_seekable_input_stream_truncate;
}

/**
 * fu_unix_seekable_input_stream_new:
 * @fd: a UNIX file descriptor
 * @close_fd: %TRUE to close the file descriptor when done
 * @error: (nullable): optional return location for an error
 *
 * Creates a new seekable GUnixInputStream for the given file descriptor.
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
	GStatBuf st = {0};
	g_autoptr(FuUnixSeekableInputStream) self = NULL;
	g_autoptr(GUnixInputStream) stream = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	stream = G_UNIX_INPUT_STREAM(g_unix_input_stream_new(fd, close_fd)); /* nocheck:blocked */

	/* check for a regular file */
	if (fstat(fd, &st) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to stat fd: %s",
			    strerror(errno));
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

	/* create wrapper */
	self = g_object_new(FU_TYPE_UNIX_SEEKABLE_INPUT_STREAM, NULL);
	self->fd = fd;
	fu_stream_input_stream_set_base_stream(FU_STREAM_INPUT_STREAM(self),
					       G_INPUT_STREAM(stream)); /* nocheck:blocked */

	/* success */
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

	fd = stream->fd;
	seals = fcntl(fd, F_GET_SEALS);
	if (seals < 0) {
		/* not supported on this fd */
		return TRUE;
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
#endif
	return TRUE;
}

static void
fu_unix_seekable_input_stream_class_init(FuUnixSeekableInputStreamClass *klass)
{
}

static void
fu_unix_seekable_input_stream_init(FuUnixSeekableInputStream *self)
{
}
