/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <gio/gio.h>
#include <string.h>

#include "fu-logitech-hidpp-common.h"

guint8
fu_logitech_hidpp_buffer_read_uint8(const gchar *str)
{
	guint64 tmp;
	gchar buf[3] = {0x0, 0x0, 0x0};
	memcpy(buf, str, 2);		       /* nocheck:blocked */
	tmp = g_ascii_strtoull(buf, NULL, 16); /* nocheck:blocked */
	return tmp;
}

guint16
fu_logitech_hidpp_buffer_read_uint16(const gchar *str)
{
	guint64 tmp;
	gchar buf[5] = {0x0, 0x0, 0x0, 0x0, 0x0};
	memcpy(buf, str, 4);		       /* nocheck:blocked */
	tmp = g_ascii_strtoull(buf, NULL, 16); /* nocheck:blocked */
	return tmp;
}

gchar *
fu_logitech_hidpp_format_version(const gchar *name, guint8 major, guint8 minor, guint16 build)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; i < 3; i++) {
		if (g_ascii_isspace(name[i]) || name[i] == '\0')
			continue;
		g_string_append_c(str, name[i]);
	}
	g_string_append_printf(str, "%02x.%02x_B%04x", major, minor, build);
	return g_string_free(str, FALSE);
}
