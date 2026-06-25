/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define g_assert_cmpfloat_with_epsilon(n1, n2, epsilon)                                            \
	g_assert(((n1) < (n2) + (epsilon)) && (n2) < ((n1) + (epsilon)))

typedef char GRefString;

typedef struct {
	gint ref_count;
	gsize len;
	gchar str[];
} FwupdRefStringImpl;

static inline char *
g_ref_string_new_len(const char *str, gssize len)
{
	FwupdRefStringImpl *impl;
	gsize len2;

	g_return_val_if_fail(str != NULL, NULL);
	len2 = (len < 0) ? strlen(str) : (gsize)len;
	impl = g_malloc0(sizeof(FwupdRefStringImpl) + len2 + 1);
	impl->len = len2;
	impl->ref_count = 1;
	/* nocheck:blocked */
	memcpy(impl->str, str, len2);
	return impl->str;
}
static inline char *
g_ref_string_new(const char *str)
{
	return g_ref_string_new_len(str, strlen(str));
}
static inline char *
g_ref_string_new_intern(const char *str)
{
	return g_ref_string_new(str);
}

#define G_REF_STRING_IMPL_FROM_STR(str)                                                            \
	((FwupdRefStringImpl *)((guint8 *)str - offsetof(FwupdRefStringImpl, str)))

static inline char *
g_ref_string_acquire(char *str)
{
	FwupdRefStringImpl *impl = G_REF_STRING_IMPL_FROM_STR(str);
	g_atomic_int_inc(&impl->ref_count);
	return str;
}
static inline void
g_ref_string_release(char *str)
{
	FwupdRefStringImpl *impl = G_REF_STRING_IMPL_FROM_STR(str);
	if (g_atomic_int_dec_and_test(&impl->ref_count))
		g_free(impl);
}
static inline gsize
g_ref_string_length(char *str)
{
	FwupdRefStringImpl *impl = G_REF_STRING_IMPL_FROM_STR(str);
	return impl->len;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GRefString, g_ref_string_release)
