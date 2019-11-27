/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuIOChannel"

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

#include "fwupd-error.h"
#include "fu-common.h"
#include "fu-io-channel.h"

struct _FuIOChannel {
	GObject			 parent_instance;
	gint			 fd;
};

G_DEFINE_TYPE (FuIOChannel, fu_io_channel, G_TYPE_OBJECT)

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
fu_io_channel_unix_get_fd (FuIOChannel *self)
{
	g_return_val_if_fail (FU_IS_IO_CHANNEL (self), -1);
	return self->fd;
}

/**
 * fu_io_channel_shutdown:
 * @self: a #FuIOChannel
 * @error: a #GError, or %NULL
 *
 * Closes the file descriptor for the device.
 *
 * Returns: %TRUE if all the FD was closed.
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_shutdown (FuIOChannel *self, GError **error)
{
	g_return_val_if_fail (FU_IS_IO_CHANNEL (self), FALSE);
	if (!g_close (self->fd, error))
		return FALSE;
	self->fd = -1;
	return TRUE;
}

static gboolean
fu_io_channel_flush_input (FuIOChannel *self, GError **error)
{
	GPollFD poll = {
		.fd = self->fd,
		.events = G_IO_IN | G_IO_ERR,
	};
	while (g_poll (&poll, 1, 0) > 0) {
		gchar c;
		gint r = read (self->fd, &c, 1);
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
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: a #GError, or %NULL
 *
 * Writes bytes to the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_write_bytes (FuIOChannel *self,
			   GBytes *bytes,
			   guint timeout_ms,
			   FuIOChannelFlags flags,
			   GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (bytes, &bufsz);
	return fu_io_channel_write_raw (self, buf, bufsz, timeout_ms, flags, error);
}

/**
 * fu_io_channel_write_byte_array:
 * @self: a #FuIOChannel
 * @buf: buffer to write
 * @timeout_ms: timeout in ms
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: a #GError, or %NULL
 *
 * Writes bytes to the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 1.3.2
 **/
gboolean
fu_io_channel_write_byte_array (FuIOChannel *self,
				GByteArray *buf,
				guint timeout_ms,
				FuIOChannelFlags flags,
				GError **error)
{
	return fu_io_channel_write_raw (self, buf->data, buf->len, timeout_ms, flags, error);
}

/**
 * fu_io_channel_write_raw:
 * @self: a #FuIOChannel
 * @data: buffer to write
 * @datasz: size of @data
 * @timeout_ms: timeout in ms
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: a #GError, or %NULL
 *
 * Writes bytes to the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: %TRUE if all the bytes was written
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_write_raw (FuIOChannel *self,
			 const guint8 *data,
			 gsize datasz,
			 guint timeout_ms,
			 FuIOChannelFlags flags,
			 GError **error)
{
	gsize idx = 0;

	g_return_val_if_fail (FU_IS_IO_CHANNEL (self), FALSE);

	/* flush pending reads */
	if (flags & FU_IO_CHANNEL_FLAG_FLUSH_INPUT) {
		if (!fu_io_channel_flush_input (self, error))
			return FALSE;
	}

	/* blocking IO */
	if (flags & FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO) {
		gssize wrote = write (self->fd, data, datasz);
		if (wrote != (gssize) datasz) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write: "
				     "wrote %" G_GSSIZE_FORMAT " of %" G_GSIZE_FORMAT,
				     wrote, datasz);
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
		rc = g_poll (&fds, 1, (gint) timeout_ms);
		if (rc == 0)
			break;
		if (rc < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to poll %i",
				     self->fd);
			return FALSE;
		}

		/* we can write data */
		if (fds.revents & G_IO_OUT) {
			gssize len = write (self->fd, data + idx, datasz - idx);
			if (len < 0) {
				if (errno == EAGAIN) {
					g_debug ("got EAGAIN, trying harder");
					continue;
				}
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to write %" G_GSIZE_FORMAT
					     " bytes to %i: %s" ,
					     datasz,
					     self->fd,
					     strerror (errno));
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
 * @max_size: maximum size of the returned blob, or -1 for no limit
 * @timeout_ms: timeout in ms
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: a #GError, or %NULL
 *
 * Reads bytes from the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: a #GBytes, or %NULL for error
 *
 * Since: 1.2.2
 **/
GBytes *
fu_io_channel_read_bytes (FuIOChannel *self,
			  gssize max_size,
			  guint timeout_ms,
			  FuIOChannelFlags flags,
			  GError **error)
{
	GByteArray *buf = fu_io_channel_read_byte_array (self,
							 max_size,
							 timeout_ms,
							 flags,
							 error);
	if (buf == NULL)
		return NULL;
	return g_byte_array_free_to_bytes (buf);
}

/**
 * fu_io_channel_read_byte_array:
 * @self: a #FuIOChannel
 * @max_size: maximum size of the returned blob, or -1 for no limit
 * @timeout_ms: timeout in ms
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: a #GError, or %NULL
 *
 * Reads bytes from the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: (transfer full): a #GByteArray, or %NULL for error
 *
 * Since: 1.3.2
 **/
GByteArray *
fu_io_channel_read_byte_array (FuIOChannel *self,
			       gssize max_size,
			       guint timeout_ms,
			       FuIOChannelFlags flags,
			       GError **error)
{
	GPollFD fds = {
		.fd = self->fd,
		.events = G_IO_IN | G_IO_PRI | G_IO_ERR,
	};
	g_autoptr(GByteArray) buf2 = g_byte_array_new ();

	g_return_val_if_fail (FU_IS_IO_CHANNEL (self), NULL);

	/* blocking IO */
	if (flags & FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO) {
		guint8 buf[1024];
		gssize len = read (self->fd, buf, sizeof (buf));
		if (len < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to read %i: %s", self->fd,
				     strerror (errno));
			return NULL;
		}
		if (len > 0)
			g_byte_array_append (buf2, buf, len);
		return g_steal_pointer (&buf2);
	}

	/* nonblocking IO */
	while (TRUE) {
		/* wait for data to appear */
		gint rc = g_poll (&fds, 1, (gint) timeout_ms);
		if (rc == 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_TIMED_OUT,
				     "timeout");
			return NULL;
		}
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to poll %i", self->fd);
			return NULL;
		}

		/* we have data to read */
		if (fds.revents & G_IO_IN) {
			guint8 buf[1024];
			gssize len = read (self->fd, buf, sizeof (buf));
			if (len < 0) {
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN)
					continue;
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "failed to read %i: %s", self->fd,
					     strerror (errno));
				return NULL;
			}
			if (len > 0)
				g_byte_array_append (buf2, buf, len);

			/* check maximum size */
			if (max_size > 0 && buf2->len >= (guint) max_size)
				break;
			if (flags & FU_IO_CHANNEL_FLAG_SINGLE_SHOT)
				break;
			continue;
		}
		if (fds.revents & G_IO_ERR) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "error condition");
			return NULL;
		}
		if (fds.revents & G_IO_HUP) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "connection hung up");
			return NULL;
		}
		if (fds.revents & G_IO_NVAL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "invalid request");
			return NULL;
		}
	}

	/* no data */
	if (buf2->len == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "no data received from device in %ums",
			     timeout_ms);
		return NULL;
	}

	/* return blob */
	return g_steal_pointer (&buf2);
}

/**
 * fu_io_channel_read_raw:
 * @self: a #FuIOChannel
 * @buf: buffer, or %NULL
 * @bufsz: size of @buf
 * @bytes_read: (out): data written to @buf, or %NULL
 * @timeout_ms: timeout in ms
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: a #GError, or %NULL
 *
 * Reads bytes from the TTY, that will fail if exceeding @timeout_ms.
 *
 * Returns: a #GBytes, or %NULL for error
 *
 * Since: 1.2.2
 **/
gboolean
fu_io_channel_read_raw (FuIOChannel *self,
			guint8 *buf,
			gsize bufsz,
			gsize *bytes_read,
			guint timeout_ms,
			FuIOChannelFlags flags,
			GError **error)
{
	const guint8 *tmpbuf = NULL;
	gsize bytes_read_tmp;
	g_autoptr(GBytes) tmp = NULL;

	g_return_val_if_fail (FU_IS_IO_CHANNEL (self), FALSE);

	tmp = fu_io_channel_read_bytes (self, bufsz, timeout_ms, flags, error);
	if (tmp == NULL)
		return FALSE;
	tmpbuf = g_bytes_get_data (tmp, &bytes_read_tmp);
	if (tmpbuf != NULL)
		memcpy (buf, tmpbuf, bytes_read_tmp);
	if (bytes_read != NULL)
		*bytes_read = bytes_read_tmp;
	return TRUE;
}

static void
fu_io_channel_finalize (GObject *object)
{
	FuIOChannel *self = FU_IO_CHANNEL (object);
	if (self->fd != -1)
		g_close (self->fd, NULL);
	G_OBJECT_CLASS (fu_io_channel_parent_class)->finalize (object);
}

static void
fu_io_channel_class_init (FuIOChannelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_io_channel_finalize;
}

static void
fu_io_channel_init (FuIOChannel *self)
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
fu_io_channel_unix_new (gint fd)
{
	FuIOChannel *self;
	self = g_object_new (FU_TYPE_IO_CHANNEL, NULL);
	self->fd = fd;
	return FU_IO_CHANNEL (self);
}

/**
 * fu_io_channel_new_file:
 * @filename: device file
 * @error: a #GError, or %NULL
 *
 * Creates a new object to write and read from.
 *
 * Returns: a #FuIOChannel
 *
 * Since: 1.2.2
 **/
FuIOChannel *
fu_io_channel_new_file (const gchar *filename, GError **error)
{
#ifdef HAVE_POLL_H
	gint fd = g_open (filename, O_RDWR | O_NONBLOCK, S_IRWXU);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s", filename);
		return NULL;
	}
	return fu_io_channel_unix_new (fd);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <poll.h> is unavailable");
	return NULL;
#endif
}
