/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <gio/gio.h>

#include "fu-unifying-common.h"

guint8
fu_unifying_buffer_read_uint8 (const gchar *str)
{
	guint64 tmp;
	gchar buf[3] = { 0x0, 0x0, 0x0 };
	memcpy (buf, str, 2);
	tmp = g_ascii_strtoull (buf, NULL, 16);
	return tmp;
}

guint16
fu_unifying_buffer_read_uint16 (const gchar *str)
{
	guint64 tmp;
	gchar buf[5] = { 0x0, 0x0, 0x0, 0x0, 0x0 };
	memcpy (buf, str, 4);
	tmp = g_ascii_strtoull (buf, NULL, 16);
	return tmp;
}

gchar *
fu_unifying_format_version (const gchar *name, guint8 major, guint8 minor, guint16 build)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 3; i++) {
		if (g_ascii_isspace (name[i]))
			continue;
		g_string_append_c (str, name[i]);
	}
	g_string_append_printf (str, "%02x.%02x_B%04x", major, minor, build);
	return g_string_free (str, FALSE);
}

static gboolean
fu_unifying_nonblock_flush (gint fd, GError **error)
{
	GPollFD poll[] = {
		{
			.fd = fd,
			.events = G_IO_IN | G_IO_OUT | G_IO_ERR,
		},
	};
	while (g_poll (poll, G_N_ELEMENTS(poll), 0) > 0) {
		gchar c;
		gint r = read (fd, &c, 1);
		if (r < 0 && errno != EINTR)
			break;
	}
	return TRUE;
}

gboolean
fu_unifying_nonblock_write (gint fd,
			    const guint8 *data,
			    gsize data_sz,
			    GError **error)
{
	gssize wrote;

	/* sanity check */
	if (fd == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write: fd is not open");
		return FALSE;
	}

	/* flush pending reads */
	if (!fu_unifying_nonblock_flush (fd, error))
		return FALSE;

	/* write */
	wrote = write (fd, data, data_sz);
	if (wrote != (gssize) data_sz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write: "
			     "wrote %" G_GSSIZE_FORMAT " of %" G_GSIZE_FORMAT,
			     wrote, data_sz);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_unifying_nonblock_read (gint fd,
			   guint8 *data,
			   gsize data_sz,
			   gsize *data_len,
			   guint timeout,
			   GError **error)
{
	gssize len = 0;
	gint64 ts_start;
	GPollFD poll[] = {
		{
			.fd = fd,
			.events = G_IO_IN | G_IO_OUT | G_IO_ERR,
		},
	};

	/* sanity check */
	if (fd == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to read: fd is not open");
		return FALSE;
	}

	/* do a read with a timeout */
	ts_start = g_get_monotonic_time ();
	while (1) {
		gint rc;
		gint ts_remain = ((g_get_monotonic_time () - ts_start) / 1000) + timeout;
		if (ts_remain <= 0) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_TIMED_OUT,
					     "timeout already passed");
			return FALSE;
		}

		/* waits for the fd to become ready */
		rc = g_poll (poll, G_N_ELEMENTS (poll), ts_remain);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "read interrupted: %s",
				     g_strerror (errno));
			return FALSE;
		} else if (rc == 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_TIMED_OUT,
				     "timeout");
			return FALSE;
		}

		/* read data from fd */
		len = read (fd, data, data_sz);
		if (len <= 0) {
			if (len == -1 && errno == EINTR)
				continue;
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to read data: %s",
				     g_strerror (errno));
			return FALSE;
		}

		/* success */
		break;
	};

	/* success */
	if (data_len != NULL)
		*data_len = (gsize) len;
	return TRUE;
}

gint
fu_unifying_nonblock_open (const gchar *filename, GError **error)
{
	gint fd;
	fd = open (filename, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open %s", filename);
		return -1;
	}
	return fd;
}
