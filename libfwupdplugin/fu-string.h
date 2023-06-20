/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

guint
fu_string_replace(GString *string, const gchar *search, const gchar *replace);
void
fu_string_append(GString *str, guint idt, const gchar *key, const gchar *value);
void
fu_string_append_ku(GString *str, guint idt, const gchar *key, guint64 value);
void
fu_string_append_kx(GString *str, guint idt, const gchar *key, guint64 value);
void
fu_string_append_kb(GString *str, guint idt, const gchar *key, gboolean value);

gchar *
fu_strsafe(const gchar *str, gsize maxsz);
gboolean
fu_strtoull(const gchar *str, guint64 *value, guint64 min, guint64 max, GError **error);
gboolean
fu_strtobool(const gchar *str, gboolean *value, GError **error);
gchar *
fu_strstrip(const gchar *str);
gsize
fu_strwidth(const gchar *text);
gchar **
fu_strsplit(const gchar *str, gsize sz, const gchar *delimiter, gint max_tokens);
gchar *
fu_strjoin(const gchar *separator, GPtrArray *array);
GString *
fu_strdup(const gchar *str, gsize bufsz, gsize offset);

/**
 * FuStrsplitFunc:
 * @token: a #GString
 * @token_idx: the token number
 * @user_data: (closure): user data
 * @error: a #GError or NULL
 *
 * The fu_strsplit_full() iteration callback.
 */
typedef gboolean (*FuStrsplitFunc)(GString *token,
				   guint token_idx,
				   gpointer user_data,
				   GError **error);
gboolean
fu_strsplit_full(const gchar *str,
		 gssize sz,
		 const gchar *delimiter,
		 FuStrsplitFunc callback,
		 gpointer user_data,
		 GError **error);

/**
 * FuUtfConvertFlags:
 * @FU_UTF_CONVERT_FLAG_NONE:		No flags set
 * @FU_UTF_CONVERT_FLAG_APPEND_NUL:	Include the trailing `NUL` or `NULw` in the buffer
 *
 * The flags to use when converting to and from UTF-8.
 **/
typedef enum {
	FU_UTF_CONVERT_FLAG_NONE = 0,
	FU_UTF_CONVERT_FLAG_APPEND_NUL = 1 << 0,
} FuUtfConvertFlags;

gchar *
fu_utf16_to_utf8_byte_array(GByteArray *array, GError **error);
GByteArray *
fu_utf8_to_utf16_byte_array(const gchar *str, FuUtfConvertFlags flags, GError **error);
gchar *
fu_utf16_to_utf8_bytes(GBytes *bytes, GError **error);
GBytes *
fu_utf8_to_utf16_bytes(const gchar *str, FuUtfConvertFlags flags, GError **error);
