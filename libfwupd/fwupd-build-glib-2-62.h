/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

static inline void
g_clear_signal_handler(gulong *handler_id_ptr, gpointer instance)
{
	if (*handler_id_ptr > 0) {
		g_signal_handler_disconnect(G_OBJECT(instance), *handler_id_ptr);
		*handler_id_ptr = 0;
	}
}

static inline gchar *
g_date_time_format_iso8601(GDateTime *datetime)
{
	GString *outstr = NULL;
	g_autofree gchar *main_date = NULL;

	g_return_val_if_fail(datetime != NULL, NULL);

	main_date = g_date_time_format(datetime, "%C%y-%m-%dT%H:%M:%S");
	outstr = g_string_new(main_date);

	if (g_date_time_get_utc_offset(datetime) == 0) {
		g_string_append_c(outstr, 'Z');
	} else {
		g_autofree gchar *time_zone = g_date_time_format(datetime, "%:::z");
		g_string_append(outstr, time_zone);
	}

	return g_string_free(outstr, FALSE);
}
