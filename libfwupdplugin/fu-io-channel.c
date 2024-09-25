/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuIOChannel"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#include <string.h>
#include <sys/stat.h>
#ifdef HAVE_MEMFD_CREATE
#include <sys/mman.h>
#endif

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-io-channel.h"
#include "fu-string.h"

/**
 * FuIOChannel:
 *
 * A bidirectional IO channel which can be read from and written to.
 */

struct _FuIOChannel {
	GObject parent_instance;
	gint fd;
};

G_DEFINE_TYPE(FuIOChannel, fu_io_channel, G_TYPE_OBJECT)

/**
 * fu_io_channel_unix_get_fd:
 * @self: a #FuIOChannel
 *
 * Gets the file descriptor for the device.
 *
 * Returns: fd, or -1 for not open.
 *
 * Since: 1.2.2
 **/
gint
fu_io_channel_unix_get_fd(FuIOChannel *self)
{
	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), -1);
	return self->fd;
}

/**
 * fu_io_channel_shutdown:
 * @self: a #FuIOChannel
 * @error: (nullable): optional return location for an error
 *
 * Closes the file descriptor for the device if open.
 *
 * Returns: %TRUE if all the FD was closed.
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_shutdown(FuIOChannel *self, GError **error)
{
	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (self->fd != -1) {
		if (!g_close(self->fd, error))
			return FALSE;
		self->fd = -1;
	}
	return TRUE;
}

/**
 * fu_io_channel_seek:
 * @self: a #FuIOChannel
 * @offset: an absolute offset in bytes
 * @error: (nullable): optional return location for an error
 *
 * Seeks the file descriptor to a specific offset.
 *
 * Returns: %TRUE if all the seek worked.
 *
 * Since: 2.0.0
 **/
gboolean
fu_io_channel_seek(FuIOChannel *self, gsize offset, GError **error)
{
	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (self->fd == -1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "channel is not open");
		return FALSE;
	}
	if (lseek(self->fd, offset, SEEK_SET) < 0) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
			    "failed to seek to 0x%04x: %s",
			    (guint)offset,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_io_channel_flush_input(FuIOChannel *self, GError **error)
{
	GPollFD poll = {
	    .fd = self->fd,
	    .events = G_IO_IN | G_IO_ERR,
	};
	while (g_poll(&poll, 1, 0) > 0) {
		gchar c;
		gint r = read(self->fd, &c, 1);
		if (r < 0 && errno != EINTR)
			break;
	}
	return TRUE;
}

/**
 * fu_io_channel_write_bytes:
 * @self: a #FuIOChannel
 * @bytes: buffer to write
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Writes bytes to the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_write_bytes(FuIOChannel *self,
			  GBytes *bytes,
			  guint timeout_ms,
			  FuIOChannelFlags flags,
			  GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(bytes, &bufsz);
	return fu_io_channel_write_raw(self, buf, bufsz, timeout_ms, flags, error);
}

typedef struct {
	FuIOChannel *self;
	guint timeout_ms;
	FuIOChannelFlags flags;
} FuIOChannelWriteStreamHelper;

static gboolean
fu_io_channel_write_stream_cb(const guint8 *buf, gsize bufsz, gpointer user_data, GError **error)
{
	FuIOChannelWriteStreamHelper *helper = (FuIOChannelWriteStreamHelper *)user_data;
	return fu_io_channel_write_raw(helper->self,
				       buf,
				       bufsz,
				       helper->timeout_ms,
				       helper->flags,
				       error);
}

/**
 * fu_io_channel_write_stream:
 * @self: a #FuIOChannel
 * @stream: #GInputStream to write
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Writes the stream to the fd, chucking when required.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 2.0.0
 **/
gboolean
fu_io_channel_write_stream(FuIOChannel *self,
			   GInputStream *stream,
			   guint timeout_ms,
			   FuIOChannelFlags flags,
			   GError **error)
{
	FuIOChannelWriteStreamHelper helper = {.self = self,
					       .timeout_ms = timeout_ms,
					       .flags = flags};
	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_input_stream_chunkify(stream, fu_io_channel_write_stream_cb, &helper, error);
}

/**
 * fu_io_channel_write_byte_array:
 * @self: a #FuIOChannel
 * @buf: buffer to write
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Writes bytes to the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 1.3.2
 **/
gboolean
fu_io_channel_write_byte_array(FuIOChannel *self,
			       GByteArray *buf,
			       guint timeout_ms,
			       FuIOChannelFlags flags,
			       GError **error)
{
	return fu_io_channel_write_raw(self, buf->data, buf->len, timeout_ms, flags, error);
}

/**
 * fu_io_channel_write_raw:
 * @self: a #FuIOChannel
 * @data: buffer to write
 * @datasz: size of @data
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Writes bytes to the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_write_raw(FuIOChannel *self,
			const guint8 *data,
			gsize datasz,
			guint timeout_ms,
			FuIOChannelFlags flags,
			GError **error)
{
	gsize idx = 0;

	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* flush pending reads */
	if (flags & FU_IO_CHANNEL_FLAG_FLUSH_INPUT) {
		if (!fu_io_channel_flush_input(self, error))
			return FALSE;
	}

	/* blocking IO */
	if (flags & FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO) {
		gssize wrote = write(self->fd, data, datasz);
		if (wrote != (gssize)datasz) {
			if (errno == EPROTO) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "failed to write: %s",
					    g_strerror(errno));
				return FALSE;
			}
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: "
				    "wrote %" G_GSSIZE_FORMAT " of %" G_GSIZE_FORMAT,
				    wrote,
				    datasz);
			return FALSE;
		}
		return TRUE;
	}

	/* nonblocking IO */
	while (idx < datasz) {
		gint rc;
		GPollFD fds = {
		    .fd = self->fd,
		    .events = G_IO_OUT | G_IO_ERR,
		};

		/* wait for data to be allowed to write without blocking */
		rc = g_poll(&fds, 1, (gint)timeout_ms);
		if (rc == 0)
			break;
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to poll %i",
				    self->fd);
			return FALSE;
		}

		/* we can write data */
		if (fds.revents & G_IO_OUT) {
			gssize len = write(self->fd, data + idx, datasz - idx);
			if (len < 0) {
				if (errno == EAGAIN) {
					g_debug("got EAGAIN, trying harder");
					continue;
				}
				if (errno == EPROTO) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_FOUND,
						    "failed to write: %s",
						    g_strerror(errno));
					return FALSE;
				}
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "failed to write %" G_GSIZE_FORMAT " bytes to %i: %s",
					    datasz,
					    self->fd,
					    g_strerror(errno));
				return FALSE;
			}
			if (flags & FU_IO_CHANNEL_FLAG_SINGLE_SHOT)
				break;
			idx += len;
		}
	}

	return TRUE;
}

/**
 * fu_io_channel_read_bytes:
 * @self: a #FuIOChannel
 * @count: number of bytes to read, or -1 for no limit
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Reads bytes from the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: a #GBytes (which may be bigger than @count), or %NULL for error
 *
 * Since: 1.2.2
 **/
GBytes *
fu_io_channel_read_bytes(FuIOChannel *self,
			 gssize count,
			 guint timeout_ms,
			 FuIOChannelFlags flags,
			 GError **error)
{
	g_autoptr(GByteArray) buf =
	    fu_io_channel_read_byte_array(self, count, timeout_ms, flags, error);
	if (buf == NULL)
		return NULL;
	return g_bytes_new(buf->data, buf->len);
}

/**
 * fu_io_channel_read_byte_array:
 * @self: a #FuIOChannel
 * @count: number of bytes to read, or -1 for no limit
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Reads bytes from the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: (transfer full): a #GByteArray (which may be bigger than @count), or %NULL for error
 *
 * Since: 1.3.2
 **/
GByteArray *
fu_io_channel_read_byte_array(FuIOChannel *self,
			      gssize count,
			      guint timeout_ms,
			      FuIOChannelFlags flags,
			      GError **error)
{
	GPollFD fds = {
	    .fd = self->fd,
	    .events = G_IO_IN | G_IO_PRI | G_IO_ERR,
	};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) buf_tmp = g_byte_array_new();

	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), NULL);

	/* a temp buf of 1k or smaller size */
	g_byte_array_set_size(buf_tmp, count >= 0 ? MIN(count, 1024) : 1024);

	/* blocking IO */
	if (flags & FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO) {
		do {
			gssize len = read(self->fd, buf_tmp->data, buf_tmp->len);
			if (len < 0) {
				g_set_error(error,
					    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
					    g_io_error_from_errno(errno),
#else
					    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
					    "failed to read %i: %s",
					    self->fd,
					    g_strerror(errno));
				fwupd_error_convert(error);
				return NULL;
			}
			if (len == 0)
				break;
			if (flags & FU_IO_CHANNEL_FLAG_SINGLE_SHOT)
				break;
			g_byte_array_append(buf, buf_tmp->data, len);
		} while (count < 0 || buf->len < (gsize)count);
		return g_steal_pointer(&buf);
	}

	/* nonblocking IO */
	while (TRUE) {
		/* wait for data to appear */
		gint rc = g_poll(&fds, 1, (gint)timeout_ms);
		if (rc == 0) {
			g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT, "timeout");
			return NULL;
		}
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to poll %i",
				    self->fd);
			return NULL;
		}

		/* we have data to read */
		if (fds.revents & G_IO_IN) {
			gssize len = read(self->fd, buf_tmp->data, buf_tmp->len);
			if (len < 0) {
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN)
					continue;
				g_set_error(error,
					    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
					    g_io_error_from_errno(errno),
#else
					    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
					    "failed to read %i: %s",
					    self->fd,
					    g_strerror(errno));
				fwupd_error_convert(error);
				return NULL;
			}
			if (len == 0)
				break;
			if (len > 0)
				g_byte_array_append(buf, buf_tmp->data, len);

			/* check maximum size */
			if (count > 0 && buf->len >= (guint)count)
				break;
			if (flags & FU_IO_CHANNEL_FLAG_SINGLE_SHOT)
				break;
			continue;
		}
		if (fds.revents & G_IO_ERR) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "error condition");
			return NULL;
		}
		if (fds.revents & G_IO_HUP) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "connection hung up");
			return NULL;
		}
		if (fds.revents & G_IO_NVAL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "invalid request");
			return NULL;
		}
	}

	/* no data */
	if (buf->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "no data received from device in %ums",
			    timeout_ms);
		return NULL;
	}

	/* return blob */
	return g_steal_pointer(&buf);
}

/**
 * fu_io_channel_read_raw:
 * @self: a #FuIOChannel
 * @buf: (nullable): optional buffer
 * @bufsz: size of @buf
 * @bytes_read: (out) (nullable): data written to @buf
 * @timeout_ms: timeout in ms
 * @flags: channel flags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Reads bytes from the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: a #GBytes, or %NULL for error
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_read_raw(FuIOChannel *self,
		       guint8 *buf,
		       gsize bufsz,
		       gsize *bytes_read,
		       guint timeout_ms,
		       FuIOChannelFlags flags,
		       GError **error)
{
	g_autoptr(GByteArray) tmp = NULL;

	g_return_val_if_fail(FU_IS_IO_CHANNEL(self), FALSE);

	tmp = fu_io_channel_read_byte_array(self, bufsz, timeout_ms, flags, error);
	if (tmp == NULL)
		return FALSE;
	if (buf != NULL)
		memcpy(buf, tmp->data, MIN(tmp->len, bufsz)); /* nocheck:blocked */
	if (bytes_read != NULL)
		*bytes_read = tmp->len;
	return TRUE;
}

static void
fu_io_channel_finalize(GObject *object)
{
	FuIOChannel *self = FU_IO_CHANNEL(object);
	if (self->fd != -1)
		g_close(self->fd, NULL);
	G_OBJECT_CLASS(fu_io_channel_parent_class)->finalize(object);
}

static void
fu_io_channel_class_init(FuIOChannelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_io_channel_finalize;
}

static void
fu_io_channel_init(FuIOChannel *self)
{
	self->fd = -1;
}

/**
 * fu_io_channel_unix_new:
 * @fd: file descriptor
 *
 * Creates a new object to write and read from.
 *
 * Returns: a #FuIOChannel
 *
 * Since: 1.2.2
 **/
FuIOChannel *
fu_io_channel_unix_new(gint fd)
{
	FuIOChannel *self;
	self = g_object_new(FU_TYPE_IO_CHANNEL, NULL);
	self->fd = fd;
	return FU_IO_CHANNEL(self);
}

/**
 * fu_io_channel_new_file:
 * @filename: device file
 * @open_flags: some #FuIoChannelOpenFlag typically %FU_IO_CHANNEL_OPEN_FLAG_READ
 * @error: (nullable): optional return location for an error
 *
 * Creates a new object to write and/or read from.
 *
 * Returns: a #FuIOChannel
 *
 * Since: 2.0.0
 **/
FuIOChannel *
fu_io_channel_new_file(const gchar *filename, FuIoChannelOpenFlag open_flags, GError **error)
{
	gint fd;
	int flags = 0;

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

#ifdef HAVE_POLL_H
	flags |= O_NONBLOCK;
#endif
	if (open_flags & FU_IO_CHANNEL_OPEN_FLAG_READ &&
	    open_flags & FU_IO_CHANNEL_OPEN_FLAG_WRITE) {
		flags |= O_RDWR;
	} else if (open_flags & FU_IO_CHANNEL_OPEN_FLAG_READ) {
		flags |= O_RDONLY;
	} else if (open_flags & FU_IO_CHANNEL_OPEN_FLAG_WRITE) {
		flags |= O_WRONLY;
	}
#ifdef O_NONBLOCK
	if (open_flags & FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK)
		flags |= O_NONBLOCK;
#endif
#ifdef O_SYNC
	if (open_flags & FU_IO_CHANNEL_OPEN_FLAG_SYNC)
		flags |= O_SYNC;
#endif
	fd = g_open(filename, flags, S_IRWXU);
	if (fd < 0) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
			    "failed to open %s: %s",
			    filename,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return NULL;
	}
	return fu_io_channel_unix_new(fd);
}

/**
 * fu_io_channel_virtual_new:
 * @name: (not nullable): memfd name
 * @error: (nullable): optional return location for an error
 *
 * Creates a new virtual object to write and/or read from.
 *
 * Returns: a #FuIOChannel
 *
 * Since: 2.0.0
 **/
FuIOChannel *
fu_io_channel_virtual_new(const gchar *name, GError **error)
{
#ifdef HAVE_MEMFD_CREATE
	gint fd;

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fd = memfd_create(name, MFD_CLOEXEC);
	if (fd < 0) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
			    "failed to create %s: %s",
			    name,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return NULL;
	}
	return fu_io_channel_unix_new(fd);
#else
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "memfd not supported");
	return NULL;
#endif
}
