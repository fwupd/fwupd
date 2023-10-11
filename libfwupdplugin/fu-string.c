/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * fu_strtoull:
 * @str: a string, e.g. `0x1234`
 * @value: (out) (nullable): parsed value
 * @min: minimum acceptable value, typically 0
 * @max: maximum acceptable value, typically G_MAXUINT64
 * @error: (nullable): optional return location for an error
 *
 * Converts a string value to an integer. Values are assumed base 10, unless
 * prefixed with "0x" where they are parsed as base 16.
 *
 * Returns: %TRUE if the value was parsed correctly, or %FALSE for error
 *
 * Since: 1.8.2
 **/
gboolean
fu_strtoull(const gchar *str, guint64 *value, guint64 min, guint64 max, GError **error)
{
	gchar *endptr = NULL;
	guint64 value_tmp;
	guint base = 10;

	/* sanity check */
	if (str == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "cannot parse NULL");
		return FALSE;
	}

	/* detect hex */
	if (g_str_has_prefix(str, "0x")) {
		str += 2;
		base = 16;
	}

	/* convert */
	value_tmp = g_ascii_strtoull(str, &endptr, base);
	if ((gsize)(endptr - str) != strlen(str) && *endptr != '\n') {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "cannot parse %s", str);
		return FALSE;
	}

	/* overflow check */
	if (value_tmp == G_MAXUINT64) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as caused overflow",
			    str);
		return FALSE;
	}

	/* range check */
	if (value_tmp < min) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "value %" G_GUINT64_FORMAT " was below minimum %" G_GUINT64_FORMAT,
			    value_tmp,
			    min);
		return FALSE;
	}
	if (value_tmp > max) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "value %" G_GUINT64_FORMAT " was above maximum %" G_GUINT64_FORMAT,
			    value_tmp,
			    max);
		return FALSE;
	}

	/* success */
	if (value != NULL)
		*value = value_tmp;
	return TRUE;
}

/**
 * fu_strtoll:
 * @str: a string, e.g. `0x1234`, `-12345`
 * @value: (out) (nullable): parsed value
 * @min: minimum acceptable value, typically 0
 * @max: maximum acceptable value, typically G_MAXINT64
 * @error: (nullable): optional return location for an error
 *
 * Converts a string value to an integer. Values are assumed base 10, unless
 * prefixed with "0x" where they are parsed as base 16.
 *
 * Returns: %TRUE if the value was parsed correctly, or %FALSE for error
 *
 * Since: 1.9.7
 **/
gboolean
fu_strtoll(const gchar *str, gint64 *value, gint64 min, gint64 max, GError **error)
{
	gchar *endptr = NULL;
	gint64 value_tmp;
	guint base = 10;

	/* sanity check */
	if (str == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "cannot parse NULL");
		return FALSE;
	}

	/* detect hex */
	if (g_str_has_prefix(str, "0x")) {
		str += 2;
		base = 16;
	}

	/* convert */
	value_tmp = g_ascii_strtoll(str, &endptr, base);
	if ((gsize)(endptr - str) != strlen(str) && *endptr != '\n') {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "cannot parse %s", str);
		return FALSE;
	}

	/* overflow check */
	if (value_tmp == G_MAXINT64) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as caused overflow",
			    str);
		return FALSE;
	}

	/* range check */
	if (value_tmp < min) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "value %" G_GINT64_FORMAT " was below minimum %" G_GINT64_FORMAT,
			    value_tmp,
			    min);
		return FALSE;
	}
	if (value_tmp > max) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "value %" G_GINT64_FORMAT " was above maximum %" G_GINT64_FORMAT,
			    value_tmp,
			    max);
		return FALSE;
	}

	/* success */
	if (value != NULL)
		*value = value_tmp;
	return TRUE;
}

/**
 * fu_strtobool:
 * @str: a string, e.g. `true`
 * @value: (out) (nullable): parsed value
 * @error: (nullable): optional return location for an error
 *
 * Converts a string value to a boolean. Only `true` and `false` are accepted values.
 *
 * Returns: %TRUE if the value was parsed correctly, or %FALSE for error
 *
 * Since: 1.8.2
 **/
gboolean
fu_strtobool(const gchar *str, gboolean *value, GError **error)
{
	/* sanity check */
	if (str == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "cannot parse NULL");
		return FALSE;
	}

	/* be super strict */
	if (g_strcmp0(str, "true") == 0) {
		if (value != NULL)
			*value = TRUE;
		return TRUE;
	}
	if (g_strcmp0(str, "false") == 0) {
		if (value != NULL)
			*value = FALSE;
		return TRUE;
	}

	/* invalid */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "cannot parse %s as boolean, expected true|false",
		    str);
	return FALSE;
}

/**
 * fu_strstrip:
 * @str: a string, e.g. ` test `
 *
 * Removes leading and trailing whitespace from a constant string.
 *
 * Returns: newly allocated string
 *
 * Since: 1.8.2
 **/
gchar *
fu_strstrip(const gchar *str)
{
	guint head = G_MAXUINT;
	guint tail = 0;

	g_return_val_if_fail(str != NULL, NULL);

	/* find first non-space char */
	for (guint i = 0; str[i] != '\0'; i++) {
		if (str[i] != ' ') {
			head = i;
			break;
		}
	}
	if (head == G_MAXUINT)
		return g_strdup("");

	/* find last non-space char */
	for (guint i = head; str[i] != '\0'; i++) {
		if (!g_ascii_isspace(str[i]))
			tail = i;
	}
	return g_strndup(str + head, tail - head + 1);
}

/**
 * fu_strdup:
 * @str: a string, e.g. ` test `
 * @bufsz: the maximum size of @str
 * @offset: the offset to start copying from
 *
 * Copies a string from a buffer of a specified size up to (but not including) `NUL`.
 *
 * Returns: (transfer full): a #GString, possibly of zero size.
 *
 * Since: 1.8.11
 **/
GString *
fu_strdup(const gchar *str, gsize bufsz, gsize offset)
{
	GString *substr;

	g_return_val_if_fail(str != NULL, NULL);
	g_return_val_if_fail(offset < bufsz, NULL);

	substr = g_string_new(NULL);
	while (offset < bufsz) {
		if (str[offset] == '\0')
			break;
		g_string_append_c(substr, str[offset++]);
	}
	return substr;
}

/**
 * fu_strwidth:
 * @text: the string to operate on
 *
 * Returns the width of the string in displayed characters on the console.
 *
 * Returns: width of text
 *
 * Since: 1.8.2
 **/
gsize
fu_strwidth(const gchar *text)
{
	const gchar *p = text;
	gsize width = 0;

	g_return_val_if_fail(text != NULL, 0);

	while (*p) {
		gunichar c = g_utf8_get_char(p);
		if (g_unichar_iswide(c))
			width += 2;
		else if (!g_unichar_iszerowidth(c))
			width += 1;
		p = g_utf8_next_char(p);
	}
	return width;
}

/**
 * fu_string_append:
 * @str: a #GString
 * @idt: the indent
 * @key: a string to append
 * @value: a string to append
 *
 * Appends a key and string value to a string
 *
 * Since: 1.8.2
 */
void
fu_string_append(GString *str, guint idt, const gchar *key, const gchar *value)
{
	const guint align = 24;
	gsize keysz;

	g_return_if_fail(idt * 2 < align);

	/* ignore */
	if (key == NULL)
		return;
	for (gsize i = 0; i < idt; i++)
		g_string_append(str, "  ");
	if (key[0] != '\0') {
		g_string_append_printf(str, "%s:", key);
		keysz = (idt * 2) + fu_strwidth(key) + 1;
	} else {
		keysz = idt * 2;
	}
	if (value != NULL) {
		g_auto(GStrv) split = NULL;
		split = g_strsplit(value, "\n", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			if (i == 0) {
				for (gsize j = keysz; j < align; j++)
					g_string_append(str, " ");
			} else {
				g_string_append(str, "\n");
				for (gsize j = 0; j < idt; j++)
					g_string_append(str, "  ");
			}
			g_string_append(str, split[i]);
		}
	}
	g_string_append(str, "\n");
}

/**
 * fu_string_append_ku:
 * @str: a #GString
 * @idt: the indent
 * @key: a string to append
 * @value: guint64
 *
 * Appends a key and unsigned integer to a string
 *
 * Since: 1.8.2
 */
void
fu_string_append_ku(GString *str, guint idt, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = g_strdup_printf("%" G_GUINT64_FORMAT, value);
	fu_string_append(str, idt, key, tmp);
}

/**
 * fu_string_append_kx:
 * @str: a #GString
 * @idt: the indent
 * @key: a string to append
 * @value: guint64
 *
 * Appends a key and hex integer to a string
 *
 * Since: 1.8.2
 */
void
fu_string_append_kx(GString *str, guint idt, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = g_strdup_printf("0x%x", (guint)value);
	fu_string_append(str, idt, key, tmp);
}

/**
 * fu_string_append_kb:
 * @str: a #GString
 * @idt: the indent
 * @key: a string to append
 * @value: Boolean
 *
 * Appends a key and boolean value to a string
 *
 * Since: 1.8.2
 */
void
fu_string_append_kb(GString *str, guint idt, const gchar *key, gboolean value)
{
	fu_string_append(str, idt, key, value ? "true" : "false");
}

/**
 * fu_strsplit:
 * @str: (not nullable): a string to split
 * @sz: size of @str, which must be more than 0
 * @delimiter: a string which specifies the places at which to split the string
 * @max_tokens: the maximum number of pieces to split @str into
 *
 * Splits a string into a maximum of @max_tokens pieces, using the given
 * delimiter. If @max_tokens is reached, the remainder of string is appended
 * to the last token.
 *
 * Returns: (transfer full): a newly-allocated NULL-terminated array of strings
 *
 * Since: 1.8.2
 **/
gchar **
fu_strsplit(const gchar *str, gsize sz, const gchar *delimiter, gint max_tokens)
{
	g_return_val_if_fail(str != NULL, NULL);
	g_return_val_if_fail(sz > 0, NULL);
	if (str[sz - 1] != '\0') {
		g_autofree gchar *str2 = g_strndup(str, sz);
		return g_strsplit(str2, delimiter, max_tokens);
	}
	return g_strsplit(str, delimiter, max_tokens);
}

/**
 * fu_strsplit_full:
 * @str: a string to split
 * @sz: size of @str, or -1 for unknown
 * @delimiter: a string which specifies the places at which to split the string
 * @callback: (scope call) (closure user_data): a #FuStrsplitFunc.
 * @user_data: user data
 * @error: (nullable): optional return location for an error
 *
 * Splits the string, calling the given function for each
 * of the tokens found. If any @callback returns %FALSE scanning is aborted.
 *
 * Use this function in preference to fu_strsplit() when the input file is untrusted,
 * and you don't want to allocate a GStrv with billions of one byte items.
 *
 * Returns: %TRUE if no @callback returned FALSE
 *
 * Since: 1.8.2
 */
gboolean
fu_strsplit_full(const gchar *str,
		 gssize sz,
		 const gchar *delimiter,
		 FuStrsplitFunc callback,
		 gpointer user_data,
		 GError **error)
{
	gsize delimiter_sz;
	gsize str_sz;
	guint found_idx = 0;
	guint token_idx = 0;

	g_return_val_if_fail(str != NULL, FALSE);
	g_return_val_if_fail(delimiter != NULL && delimiter[0] != '\0', FALSE);
	g_return_val_if_fail(callback != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* make known */
	str_sz = sz != -1 ? (gsize)sz : strlen(str);
	delimiter_sz = strlen(delimiter);

	/* cannot split */
	if (delimiter_sz > str_sz) {
		g_autoptr(GString) token = g_string_new(str);
		return callback(token, token_idx, user_data, error);
	}

	/* start splittin' */
	for (gsize i = 0; i < (str_sz - delimiter_sz) + 1;) {
		if (strncmp(str + i, delimiter, delimiter_sz) == 0) {
			g_autoptr(GString) token = g_string_new(NULL);
			g_string_append_len(token, str + found_idx, i - found_idx);
			if (!callback(token, token_idx++, user_data, error))
				return FALSE;
			i += delimiter_sz;
			found_idx = i;
		} else {
			i++;
		}
	}

	/* any bits left over? */
	if (found_idx != str_sz) {
		g_autoptr(GString) token = g_string_new(NULL);
		g_string_append_len(token, str + found_idx, str_sz - found_idx);
		if (!callback(token, token_idx, user_data, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_strsafe:
 * @str: (nullable): a string to make safe for printing
 * @maxsz: maximum size of returned string
 *
 * Converts a string into something that can be safely printed.
 *
 * Returns: (transfer full): safe string, or %NULL if there was nothing valid
 *
 * Since: 1.8.2
 **/
gchar *
fu_strsafe(const gchar *str, gsize maxsz)
{
	gboolean valid = FALSE;
	g_autoptr(GString) tmp = NULL;

	/* sanity check */
	if (str == NULL || maxsz == 0)
		return NULL;

	/* replace non-printable chars with '.' */
	tmp = g_string_sized_new(maxsz);
	for (gsize i = 0; i < maxsz && str[i] != '\0'; i++) {
		if (!g_ascii_isprint(str[i])) {
			g_string_append_c(tmp, '.');
			continue;
		}
		g_string_append_c(tmp, str[i]);
		if (!g_ascii_isspace(str[i]))
			valid = TRUE;
	}

	/* if just junk, don't return 'all dots' */
	if (tmp->len == 0 || !valid)
		return NULL;
	return g_string_free(g_steal_pointer(&tmp), FALSE);
}

/**
 * fu_strjoin:
 * @separator: (nullable): string to insert between each of the strings
 * @array: (element-type utf8): a #GPtrArray
 *
 * Joins an array of strings together to form one long string, with the optional
 * separator inserted between each of them.
 *
 * If @array has no items, the return value will be an empty string.
 * If @array contains a single item, separator will not appear in the resulting
 * string.
 *
 * Returns: a string
 *
 * Since: 1.8.2
 **/
gchar *
fu_strjoin(const gchar *separator, GPtrArray *array)
{
	g_autofree const gchar **strv = NULL;

	g_return_val_if_fail(array != NULL, NULL);

	strv = g_new0(const gchar *, array->len + 1);
	for (guint i = 0; i < array->len; i++)
		strv[i] = g_ptr_array_index(array, i);
	return g_strjoinv(separator, (gchar **)strv);
}

/**
 * fu_utf16_to_utf8_byte_array:
 * @array: a #GByteArray
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Converts a UTF-16 buffer to a UTF-8 string.
 *
 * Returns: (transfer full): a string, or %NULL on error
 *
 * Since: 1.9.3
 **/
gchar *
fu_utf16_to_utf8_byte_array(GByteArray *array, FuEndianType endian, GError **error)
{
	g_autofree guint16 *buf16 = NULL;

	g_return_val_if_fail(array != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (array->len % 2 != 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid UTF-16 buffer length");
		return NULL;
	}
	buf16 = g_new0(guint16, (array->len / sizeof(guint16)) + 1);
	for (guint i = 0; i < array->len / 2; i++) {
		guint16 data = fu_memread_uint16(array->data + (i * 2), endian);
		fu_memwrite_uint16((guint8 *)(buf16 + i), data, G_BYTE_ORDER);
	}
	return g_utf16_to_utf8(buf16, array->len / sizeof(guint16), NULL, NULL, error);
}

/**
 * fu_utf8_to_utf16_byte_array:
 * @str: a UTF-8 string
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @flags: a FuUtfConvertFlags, e.g. %FU_UTF_CONVERT_FLAG_APPEND_NUL
 * @error: (nullable): optional return location for an error
 *
 * Converts UTF-8 string to a buffer of UTF-16, optionially including the trailing NULw.
 *
 * Returns: (transfer full): a #GByteArray, or %NULL on error
 *
 * Since: 1.9.3
 **/
GByteArray *
fu_utf8_to_utf16_byte_array(const gchar *str,
			    FuEndianType endian,
			    FuUtfConvertFlags flags,
			    GError **error)
{
	glong buf_utf16sz = 0;
	g_autoptr(GByteArray) array = g_byte_array_new();
	g_autofree gunichar2 *buf_utf16 = NULL;

	g_return_val_if_fail(str != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	buf_utf16 = g_utf8_to_utf16(str, (glong)-1, NULL, &buf_utf16sz, error);
	if (buf_utf16 == NULL)
		return NULL;
	if (flags & FU_UTF_CONVERT_FLAG_APPEND_NUL)
		buf_utf16sz += 1;
	for (glong i = 0; i < buf_utf16sz; i++) {
		guint16 data = fu_memread_uint16((guint8 *)(buf_utf16 + i), G_BYTE_ORDER);
		fu_byte_array_append_uint16(array, data, endian);
	}
	return g_steal_pointer(&array);
}

/**
 * fu_utf16_to_utf8_bytes:
 * @bytes: a #GBytes
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Converts a UTF-16 buffer to a UTF-8 string.
 *
 * Returns: (transfer full): a string, or %NULL on error
 *
 * Since: 1.9.3
 **/
gchar *
fu_utf16_to_utf8_bytes(GBytes *bytes, FuEndianType endian, GError **error)
{
	GByteArray array = {0x0};

	g_return_val_if_fail(bytes != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	array.data = (guint8 *)g_bytes_get_data(bytes, NULL);
	array.len = g_bytes_get_size(bytes);
	return fu_utf16_to_utf8_byte_array(&array, endian, error);
}

/**
 * fu_utf8_to_utf16_bytes:
 * @str: a UTF-8 string
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Converts UTF-8 string to a buffer of UTF-16, optionally including the trailing NULw.
 *
 * Returns: (transfer full): a #GBytes, or %NULL on error
 *
 * Since: 1.9.3
 **/
GBytes *
fu_utf8_to_utf16_bytes(const gchar *str,
		       FuEndianType endian,
		       FuUtfConvertFlags flags,
		       GError **error)
{
	g_autoptr(GByteArray) buf = NULL;

	g_return_val_if_fail(str != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	buf = fu_utf8_to_utf16_byte_array(str, endian, flags, error);
	if (buf == NULL)
		return NULL;
	return g_bytes_new(buf->data, buf->len);
}
