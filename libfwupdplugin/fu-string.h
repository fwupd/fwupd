/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-common.h"

void
fu_string_append(GString *str, guint idt, const gchar *key, const gchar *value) G_GNUC_NON_NULL(1);
void
fu_string_append_ku(GString *str, guint idt, const gchar *key, guint64 value) G_GNUC_NON_NULL(1);
void
fu_string_append_kx(GString *str, guint idt, const gchar *key, guint64 value) G_GNUC_NON_NULL(1);
void
fu_string_append_kb(GString *str, guint idt, const gchar *key, gboolean value) G_GNUC_NON_NULL(1);

gchar *
fu_strsafe(const gchar *str, gsize maxsz);
gchar *
fu_strpassmask(const gchar *str) G_GNUC_NON_NULL(1);
gboolean
fu_strtoull(const gchar *str, guint64 *value, guint64 min, guint64 max, GError **error);
gboolean
fu_strtoll(const gchar *str, gint64 *value, gint64 min, gint64 max, GError **error);
gboolean
fu_strtobool(const gchar *str, gboolean *value, GError **error);
gchar *
fu_strstrip(const gchar *str) G_GNUC_NON_NULL(1);
gsize
fu_strwidth(const gchar *text) G_GNUC_NON_NULL(1);
gchar **
fu_strsplit(const gchar *str, gsize sz, const gchar *delimiter, gint max_tokens) G_GNUC_NON_NULL(1);
gchar *
fu_strjoin(const gchar *separator, GPtrArray *array) G_GNUC_NON_NULL(1, 2);
GString *
fu_strdup(const gchar *str, gsize bufsz, gsize offset) G_GNUC_NON_NULL(1);
void
fu_string_strip(GString *str) G_GNUC_NON_NULL(1);

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
		 GError **error) G_GNUC_NON_NULL(1, 3);

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
fu_utf16_to_utf8_byte_array(GByteArray *array, FuEndianType endian, GError **error)
    G_GNUC_NON_NULL(1);
GByteArray *
fu_utf8_to_utf16_byte_array(const gchar *str,
			    FuEndianType endian,
			    FuUtfConvertFlags flags,
			    GError **error) G_GNUC_NON_NULL(1);
gchar *
fu_utf16_to_utf8_bytes(GBytes *bytes, FuEndianType endian, GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_utf8_to_utf16_bytes(const gchar *str,
		       FuEndianType endian,
		       FuUtfConvertFlags flags,
		       GError **error) G_GNUC_NON_NULL(1);
