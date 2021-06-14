/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-redfish-common.h"

gchar *
fu_redfish_common_buffer_to_ipv4 (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 4; i++) {
		g_string_append_printf (str, "%u", buffer[i]);
		if (i != 3)
			g_string_append (str, ".");
	}
	return g_string_free (str, FALSE);
}

gchar *
fu_redfish_common_buffer_to_ipv6 (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 16; i += 4) {
		g_string_append_printf (str, "%02x%02x%02x%02x",
					buffer[i+0], buffer[i+1],
					buffer[i+2], buffer[i+3]);
		if (i != 12)
			g_string_append (str, ":");
	}
	return g_string_free (str, FALSE);
}

/* vim: set noexpandtab: */
