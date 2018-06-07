/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <gio/gio.h>

#include "lu-common.h"

guint8
lu_buffer_read_uint8 (const gchar *str)
{
	guint64 tmp;
	gchar buf[3] = { 0x0, 0x0, 0x0 };
	memcpy (buf, str, 2);
	tmp = g_ascii_strtoull (buf, NULL, 16);
	return tmp;
}

guint16
lu_buffer_read_uint16 (const gchar *str)
{
	guint64 tmp;
	gchar buf[5] = { 0x0, 0x0, 0x0, 0x0, 0x0 };
	memcpy (buf, str, 4);
	tmp = g_ascii_strtoull (buf, NULL, 16);
	return tmp;
}

void
lu_dump_raw (const gchar *title, const guint8 *data, gsize len)
{
	g_autoptr(GString) str = g_string_new (NULL);
	if (len == 0)
		return;
	g_string_append_printf (str, "%s:", title);
	for (gsize i = strlen (title); i < 16; i++)
		g_string_append (str, " ");
	for (gsize i = 0; i < len; i++) {
		g_string_append_printf (str, "%02x ", data[i]);
		if (i > 0 && i % 32 == 0)
			g_string_append (str, "\n");
	}
	g_debug ("%s", str->str);
}

gchar *
lu_format_version (const gchar *name, guint8 major, guint8 minor, guint16 build)
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
lu_nonblock_flush (gint fd, GError **error)
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
lu_nonblock_write (gint fd,
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
	if (!lu_nonblock_flush (fd, error))
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
lu_nonblock_read (gint fd,
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
lu_nonblock_open (const gchar *filename, GError **error)
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
