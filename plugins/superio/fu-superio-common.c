/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/errno.h>
#include <unistd.h>

#include "fu-superio-common.h"

gboolean
fu_superio_outb (gint fd, guint16 port, guint8 data, GError **error)
{
	if (pwrite (fd, &data, 1, (goffset) port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write to port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_superio_inb (gint fd, guint16 port, guint8 *data, GError **error)
{
	if (pread (fd, data, 1, (goffset) port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to read from port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_superio_regval (gint fd, guint16 port, guint8 addr,
		   guint8 *data, GError **error)
{
	if (!fu_superio_outb (fd, port, addr, error))
		return FALSE;
	if (!fu_superio_inb (fd, port + 1, data, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_superio_regval16 (gint fd, guint16 port, guint8 addr,
		     guint16 *data, GError **error)
{
	guint8 msb;
	guint8 lsb;
	if (!fu_superio_regval (fd, port, addr, &msb, error))
		return FALSE;
	if (!fu_superio_regval (fd, port, addr + 1, &lsb, error))
		return FALSE;
	*data = ((guint16) msb << 8) | (guint16) lsb;
	return TRUE;
}

gboolean
fu_superio_regwrite (gint fd, guint16 port, guint8 addr,
		     guint8 data, GError **error)
{
	if (!fu_superio_outb (fd, port, addr, error))
		return FALSE;
	if (!fu_superio_outb (fd, port + 1, data, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_superio_set_ldn (gint fd, guint16 port, guint8 ldn, GError **error)
{
	return fu_superio_regwrite (fd, port, LDN_SEL, ldn, error);
}

gboolean
fu_superio_regdump (gint fd, guint16 port, guint8 ldn, GError **error)
{
	g_autofree gchar *title = NULL;
	guint8 buf[0xff] = { 0x00 };

	/* set LDN */
	if (!fu_superio_set_ldn (fd, port, ldn, error))
		return FALSE;
	for (guint i = 0x00; i < 0xff; i++) {
		if (!fu_superio_regval (fd, port, i, &buf[i], error))
			return FALSE;
	}
	title = g_strdup_printf ("PORT:0x%04x LDN:0x%02x", port, ldn);
	fu_common_dump_raw (G_LOG_DOMAIN, title, buf, 0x100);
	return TRUE;
}
