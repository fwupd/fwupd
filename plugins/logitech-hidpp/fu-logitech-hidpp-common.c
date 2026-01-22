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
