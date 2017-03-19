/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>

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
lu_format_version (guint8 major, guint8 minor, guint16 micro)
{
	return g_strdup_printf ("%03x.%03x.%05x", major, minor, micro);
}
