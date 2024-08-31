/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-chunk-array.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

/**
 * fu_strtoull:
 * @str: a string, e.g. `0x1234`
 * @value: (out) (nullable): parsed value
 * @min: minimum acceptable value, typically 0
 * @max: maximum acceptable value, typically G_MAXUINT64
 * @base: default log base, usually %FU_INTEGER_BASE_AUTO
 * @error: (nullable): optional return location for an error
 *
 * Converts a string value to an integer. If the @value is prefixed with `0x` then the base is
 * set to 16 automatically.
 *
 * Returns: %TRUE if the value was parsed correctly, or %FALSE for error
 *
 * Since: 2.0.0
 **/
gboolean
fu_strtoull(const gchar *str,
	    guint64 *value,
	    guint64 min,
	    guint64 max,
	    FuIntegerBase base,
	    GError **error)
{
	gchar *endptr = NULL;
	guint64 value_tmp;

	/* sanity check */
	if (str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cannot parse NULL");
		return FALSE;
	}

	/* detect hex */
	if (base == FU_INTEGER_BASE_AUTO) {
		if (g_str_has_prefix(str, "0x")) {
			str += 2;
			base = FU_INTEGER_BASE_16;
		} else {
			base = FU_INTEGER_BASE_10;
		}
	} else if (base == FU_INTEGER_BASE_16 && g_str_has_prefix(str, "0x")) {
		str += 2;
	} else if (base == FU_INTEGER_BASE_10 && g_str_has_prefix(str, "0x")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cannot parse 0x-prefixed base-10 string");
		return FALSE;
	}

	/* convert */
	value_tmp = g_ascii_strtoull(str, &endptr, base); /* nocheck:blocked */
	if ((gsize)(endptr - str) != strlen(str) && *endptr != '\n') {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "cannot parse %s", str);
		return FALSE;
	}

	/* overflow check */
	if (value_tmp == G_MAXUINT64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot parse %s as caused overflow",
			    str);
		return FALSE;
	}

	/* range check */
	if (value_tmp < min) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "value %" G_GUINT64_FORMAT " was below minimum %" G_GUINT64_FORMAT,
			    value_tmp,
			    min);
		return FALSE;
	}
	if (value_tmp > max) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
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
 * @base: default log base, usually %FU_INTEGER_BASE_AUTO
 * @error: (nullable): optional return location for an error
 *
 * Converts a string value to an integer. Values are assumed base 10, unless
 * prefixed with "0x" where they are parsed as base 16.
 *
 * Returns: %TRUE if the value was parsed correctly, or %FALSE for error
 *
 * Since: 2.0.0
 **/
gboolean
fu_strtoll(const gchar *str,
	   gint64 *value,
	   gint64 min,
	   gint64 max,
	   FuIntegerBase base,
	   GError **error)
{
	gchar *endptr = NULL;
	gint64 value_tmp;

	/* sanity check */
	if (str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cannot parse NULL");
		return FALSE;
	}

	/* detect hex */
	if (base == FU_INTEGER_BASE_AUTO) {
		if (g_str_has_prefix(str, "0x")) {
			str += 2;
			base = FU_INTEGER_BASE_16;
		} else {
			base = FU_INTEGER_BASE_10;
		}
	} else if (base == FU_INTEGER_BASE_16 && g_str_has_prefix(str, "0x")) {
		str += 2;
	} else if (base == FU_INTEGER_BASE_10 && g_str_has_prefix(str, "0x")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cannot parse 0x-prefixed base-10 string");
		return FALSE;
	}

	/* convert */
	value_tmp = g_ascii_strtoll(str, &endptr, base); /* nocheck:blocked */
	if ((gsize)(endptr - str) != strlen(str) && *endptr != '\n') {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "cannot parse %s", str);
		return FALSE;
	}

	/* overflow check */
	if (value_tmp == G_MAXINT64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot parse %s as caused overflow",
			    str);
		return FALSE;
	}

	/* range check */
	if (value_tmp < min) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "value %" G_GINT64_FORMAT " was below minimum %" G_GINT64_FORMAT,
			    value_tmp,
			    min);
		return FALSE;
	}
	if (value_tmp > max) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
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

typedef struct {
	FuStrsplitFunc callback;
	gpointer user_data;
	guint token_idx;
	const gchar *delimiter;
	gsize delimiter_sz;
	gboolean detected_nul;
} FuStrsplitHelper;

static gboolean
fu_strsplit_buffer_drain(GByteArray *buf, FuStrsplitHelper *helper, GError **error)
{
	gsize buf_offset = 0;
	while (buf_offset < buf->len) {
		gsize offset;
		g_autoptr(GString) token = g_string_new(NULL);

		/* find first match in buffer, starting at the buffer offset */
		for (offset = buf_offset; offset < buf->len; offset++) {
			if (buf->data[offset] == 0x0) {
				helper->detected_nul = TRUE;
				break;
			}
			if (strncmp((const gchar *)buf->data + offset,
				    helper->delimiter,
				    helper->delimiter_sz) == 0)
				break;
		}

		/* no token found, keep going */
		if (offset == buf->len)
			break;

		/* sanity check is valid UTF-8 */
		g_string_append_len(token,
				    (const gchar *)buf->data + buf_offset,
				    offset - buf_offset);
		if (!g_utf8_validate_len(token->str, token->len, NULL)) {
			g_debug("ignoring invalid UTF-8, got: %s", token->str);
		} else {
			if (!helper->callback(token, helper->token_idx++, helper->user_data, error))
				return FALSE;
		}
		if (helper->detected_nul) {
			buf_offset = buf->len;
			break;
		}
		buf_offset = offset + helper->delimiter_sz;
	}
	g_byte_array_remove_range(buf, 0, MIN(buf_offset, buf->len));
	return TRUE;
}

/**
 * fu_strsplit_stream:
 * @stream: a #GInputStream to split
 * @offset: offset into @stream
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
 * Since: 2.0.0
 */
gboolean
fu_strsplit_stream(GInputStream *stream,
		   gsize offset,
		   const gchar *delimiter,
		   FuStrsplitFunc callback,
		   gpointer user_data,
		   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GInputStream) stream_partial = NULL;
	FuStrsplitHelper helper = {
	    .callback = callback,
	    .user_data = user_data,
	    .delimiter = delimiter,
	    .token_idx = 0,
	};

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(delimiter != NULL && delimiter[0] != '\0', FALSE);
	g_return_val_if_fail(callback != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	helper.delimiter_sz = strlen(delimiter);
	if (offset > 0) {
		gsize streamsz = 0;
		if (!fu_input_stream_size(stream, &streamsz, error))
			return FALSE;
		stream_partial =
		    fu_partial_input_stream_new(stream, offset, streamsz - offset, error);
		if (stream_partial == NULL)
			return FALSE;
	} else {
		stream_partial = g_object_ref(stream);
	}
	chunks = fu_chunk_array_new_from_stream(stream_partial, 0x0, 0x8000, error);
	if (chunks == NULL)
		return FALSE;
	for (gsize i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		if (!fu_strsplit_buffer_drain(buf, &helper, error))
			return FALSE;
		if (helper.detected_nul)
			break;
	}
	return TRUE;
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
	gsize offset_old = 0;
	gsize str_sz;
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
	while (offset_old <= str_sz) {
		gsize offset;
		g_autoptr(GString) token = g_string_new(NULL);

		for (offset = offset_old; offset < str_sz; offset++) {
			if (strncmp(str + offset, delimiter, delimiter_sz) == 0)
				break;
		}
		g_string_append_len(token, str + offset_old, offset - offset_old);
		if (!callback(token, token_idx++, user_data, error))
			return FALSE;
		offset_old = offset + delimiter_sz;
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
 * fu_strpassmask:
 * @str: (nullable): a string to make safe for printing
 *
 * Hides password strings encoded in HTTP requests.
 *
 * Returns: a string
 *
 * Since: 1.9.10
 **/
gchar *
fu_strpassmask(const gchar *str)
{
	g_autoptr(GString) tmp = g_string_new(str);
	if (tmp->str != NULL && g_strstr_len(tmp->str, -1, "@") != NULL &&
	    g_strstr_len(tmp->str, -1, ":") != NULL) {
		gboolean is_password = FALSE;
		gboolean is_url = FALSE;
		for (guint i = 0; i < tmp->len; i++) {
			const gchar *url_prefixes[] = {"http://", "https://", NULL};
			for (guint j = 0; url_prefixes[j] != NULL; j++) {
				if (g_str_has_prefix(tmp->str + i, url_prefixes[j])) {
					is_url = TRUE;
					i += strlen(url_prefixes[j]);
					break;
				}
			}
			if (tmp->str[i] == ' ' || tmp->str[i] == '@' || tmp->str[i] == '/') {
				is_url = FALSE;
				is_password = FALSE;
				continue;
			}
			if (is_url && tmp->str[i] == ':') {
				is_password = TRUE;
				continue;
			}
			if (is_url && is_password) {
				if (tmp->str[i] == '@') {
					is_password = FALSE;
					continue;
				}
				tmp->str[i] = 'X';
			}
		}
	}
	return g_string_free(g_steal_pointer(&tmp), FALSE);
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
