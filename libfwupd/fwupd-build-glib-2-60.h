/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define G_TEST_OPTION_ISOLATE_DIRS "isolate-dirs"

static inline gboolean
g_utf8_validate_len(const gchar *str, gsize max_len, const gchar **end)
{
	if (!g_utf8_validate(str, (gssize)max_len, end))
		return FALSE;
	for (gsize i = 0; i < max_len; i++) {
		if (str[i] == '\0')
			return FALSE;
	}
	return TRUE;
}

static inline void
g_ptr_array_extend(GPtrArray *array_to_extend, GPtrArray *array, GCopyFunc func, gpointer user_data)
{
	for (guint i = 0; i < array->len; i++) {
		gpointer item = g_ptr_array_index(array, i);
		g_ptr_array_add(array_to_extend, func(item, user_data));
	}
}
