/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define g_memdup2 g_memdup

static inline guint
g_string_replace(GString *string, const gchar *find, const gchar *replace, guint limit)
{
	g_auto(GStrv) strv = NULL;
	g_autofree gchar *str2 = NULL;
	g_return_val_if_fail(string != NULL, 0);
	g_return_val_if_fail(find != NULL, 0);
	g_return_val_if_fail(replace != NULL, 0);
	if (g_strcmp0(find, "") == 0)
		return 0;
	strv = g_strsplit(string->str, find, limit + 1);
	str2 = g_strjoinv(replace, strv);
	g_string_assign(string, str2);
	return g_strv_length(strv) - 1;
}

typedef GPtrArray GStrvBuilder;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GStrvBuilder, g_ptr_array_unref)

#define g_strv_builder_new() g_ptr_array_new_with_free_func(g_free)

#define g_strv_builder_add(b, v) g_ptr_array_add(b, g_strdup(v));

static inline gchar **
g_strv_builder_end(GStrvBuilder *builder)
{
	gchar **tmp;
	g_return_val_if_fail(builder != NULL, NULL);
	tmp = g_new0(gchar *, builder->len + 1);
	for (guint i = 0; i < builder->len; i++) {
		const gchar *str = g_ptr_array_index(builder, i);
		tmp[i] = g_strdup(str);
	}
	return tmp;
}
