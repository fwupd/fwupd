/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-json-array-private.h"
#include "fwupd-json-node-private.h"
#include "fwupd-json-object-private.h"
#include "fwupd-json-parser.h"

/**
 * FwupdJsonParser:
 *
 * A streaming tokenizer JSON parser that is resistant to malicious input.
 *
 * One item of note is that most of the JSON string methods actually return a #GRefString -- which
 * can be used to avoid lots of tiny memory allocation when parsing JSON into other objects.
 *
 * See also: [struct@FwupdJsonArray] [struct@FwupdJsonObject] [struct@FwupdJsonNode]
 */

struct _FwupdJsonParser {
	GObject parent_instance;
	guint max_depth;
	guint max_items;
};

G_DEFINE_TYPE(FwupdJsonParser, fwupd_json_parser, G_TYPE_OBJECT)

/**
 * fwupd_json_parser_set_max_depth:
 * @self: a #FwupdJsonParser
 * @max_depth: max nesting depth
 *
 * Sets the maximum nesting depth. By default there is no limit.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_parser_set_max_depth(FwupdJsonParser *self, guint max_depth)
{
	g_return_if_fail(FWUPD_IS_JSON_PARSER(self));
	self->max_depth = max_depth;
}

/**
 * fwupd_json_parser_set_max_items:
 * @self: a #FwupdJsonParser
 * @max_items: max items
 *
 * Sets the maximum number of items in an array or object. By default there is no limit.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_parser_set_max_items(FwupdJsonParser *self, guint max_items)
{
	g_return_if_fail(FWUPD_IS_JSON_PARSER(self));
	self->max_items = max_items;
}

typedef enum {
	FWUPD_JSON_PARSER_TOKEN_INVALID = 0,
	FWUPD_JSON_PARSER_TOKEN_RAW = 'b',
	FWUPD_JSON_PARSER_TOKEN_STRING = '\"',
	FWUPD_JSON_PARSER_TOKEN_OBJECT_START = '{',
	FWUPD_JSON_PARSER_TOKEN_OBJECT_END = '}',
	FWUPD_JSON_PARSER_TOKEN_OBJECT_DELIM = ':',
	FWUPD_JSON_PARSER_TOKEN_ARRAY_START = '[',
	FWUPD_JSON_PARSER_TOKEN_ARRAY_END = ']',
} FwupdJsonParserToken;

typedef struct {
	FwupdJsonLoadFlags flags;
	GByteArray *buf;
	gsize buf_offset; /* into @buf */
	GInputStream *stream;
	GString *acc;
	gboolean is_quoted;
	gboolean is_escape;
	guint linecnt;
	guint depth;
} FwupdJsonParserHelper;

static FwupdJsonParserHelper *
fwupd_json_parser_helper_new(void)
{
	FwupdJsonParserHelper *helper = g_new0(FwupdJsonParserHelper, 1);
	helper->linecnt = 1;
	helper->buf = g_byte_array_new();
	helper->acc = g_string_sized_new(128);
	helper->buf_offset = G_MAXSIZE;
	g_byte_array_set_size(helper->buf, 32 * 1024);
	return helper;
}

static void
fwupd_json_parser_helper_free(FwupdJsonParserHelper *helper)
{
	if (helper->stream != NULL)
		g_object_unref(helper->stream);
	g_byte_array_unref(helper->buf);
	g_string_free(helper->acc, TRUE);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdJsonParserHelper, fwupd_json_parser_helper_free)

static void
fwupd_json_parser_helper_dump_acc(FwupdJsonParserHelper *helper,
				  FwupdJsonParserToken *token,
				  GRefString **str)
{
	if (helper->is_quoted) {
		*token = FWUPD_JSON_PARSER_TOKEN_STRING;
		if (str != NULL)
			*str = g_ref_string_new_len(helper->acc->str, helper->acc->len);
	} else {
		if (helper->acc->len == 0)
			return;
		if (g_ascii_strncasecmp(helper->acc->str, "null", helper->acc->len) == 0) {
			*token = FWUPD_JSON_PARSER_TOKEN_STRING;
		} else {
			*token = FWUPD_JSON_PARSER_TOKEN_RAW;
			if (str != NULL)
				*str = g_ref_string_new_len(helper->acc->str, helper->acc->len);
		}
	}
	g_string_truncate(helper->acc, 0);
}

static gboolean
fwupd_json_parser_helper_slurp(FwupdJsonParserHelper *helper, GError **error)
{
	gssize rc;

	rc = g_input_stream_read(helper->stream, helper->buf->data, helper->buf->len, NULL, error);
	if (rc < 0) {
		fwupd_error_convert(error);
		return FALSE;
	}
	if (rc == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "incomplete data from stream");
		return FALSE;
	}
	g_byte_array_set_size(helper->buf, rc);

	/* success */
	helper->buf_offset = 0;
	return TRUE;
}

static gchar
fwupd_json_parser_unescape_char(gchar data)
{
	if (data == 'n')
		return '\n';
	if (data == 't')
		return '\t';
	if (data == '\\')
		return '\\';
	return 0;
}

static gboolean
fwupd_json_parser_helper_get_next_token_chunk(FwupdJsonParserHelper *helper,
					      FwupdJsonParserToken *token,
					      GRefString **str,
					      gboolean *buf_offset_enable,
					      GError **error)
{
	gchar data;

	/* need more data */
	if (G_UNLIKELY(helper->buf_offset >= helper->buf->len)) {
		if (!fwupd_json_parser_helper_slurp(helper, error))
			return FALSE;
	}
	data = helper->buf->data[helper->buf_offset];

	/* quotes */
	if (data == '"') {
		if (helper->is_quoted) {
			fwupd_json_parser_helper_dump_acc(helper, token, str);
			helper->is_quoted = FALSE;
			return TRUE;
		}
		helper->is_quoted = TRUE;
		return TRUE;
	}
	if (helper->is_quoted) {
		/* escape char */
		if (!helper->is_escape && data == '\\') {
			helper->is_escape = TRUE;
			return TRUE;
		}
		if (G_UNLIKELY(helper->is_escape)) {
			data = fwupd_json_parser_unescape_char(data);
			if (G_UNLIKELY(data == 0)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid escape char '%c'",
					    data);
				return FALSE;
			}
			helper->is_escape = FALSE;
		}

		/* save acc */
		g_string_append_c(helper->acc, data);
		return TRUE;
	}

	/* newline, for error messages */
	if (data == '\n') {
		helper->linecnt++;
		fwupd_json_parser_helper_dump_acc(helper, token, str);
		return TRUE;
	}

	/* split */
	if (data == ',') {
		fwupd_json_parser_helper_dump_acc(helper, token, str);
		return TRUE;
	}

	/* control token */
	if (data == FWUPD_JSON_PARSER_TOKEN_ARRAY_START ||
	    data == FWUPD_JSON_PARSER_TOKEN_ARRAY_END ||
	    data == FWUPD_JSON_PARSER_TOKEN_OBJECT_START ||
	    data == FWUPD_JSON_PARSER_TOKEN_OBJECT_DELIM ||
	    data == FWUPD_JSON_PARSER_TOKEN_OBJECT_END) {
		fwupd_json_parser_helper_dump_acc(helper, token, str);
		if (*token != FWUPD_JSON_PARSER_TOKEN_INVALID) {
			*buf_offset_enable = FALSE;
			return TRUE;
		}
		*token = data;
		return TRUE;
	}

	/* whitespace */
	if (g_ascii_isspace(data))
		return TRUE;

	/* strip control chars */
	if (g_ascii_iscntrl(data)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "ASCII control character detected 0x%x",
			    (guint)data);
		return FALSE;
	}

	/* save acc */
	g_string_append_c(helper->acc, data);
	return TRUE;
}

static gboolean
fwupd_json_parser_helper_get_next_token(FwupdJsonParserHelper *helper,
					FwupdJsonParserToken *token,
					GRefString **str,
					GError **error)
{
	/* process each byte until we get a token */
	while (*token == FWUPD_JSON_PARSER_TOKEN_INVALID) {
		gboolean buf_offset_enable = TRUE;
		if (!fwupd_json_parser_helper_get_next_token_chunk(helper,
								   token,
								   str,
								   &buf_offset_enable,
								   error))
			return FALSE;
		if (buf_offset_enable)
			helper->buf_offset++;
	}

	/* success */
	return TRUE;
}

static gboolean
fwupd_json_parser_helper_check_depth(FwupdJsonParser *self, guint depth, GError **error)
{
	if (self->max_depth > 0 && depth > self->max_depth) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "structure too deep, limit was %u",
			    depth);
		return FALSE;
	}
	return TRUE;
}

static FwupdJsonObject *
fwupd_json_parser_load_object(FwupdJsonParser *self, FwupdJsonParserHelper *helper, GError **error);

static FwupdJsonArray *
fwupd_json_parser_load_array(FwupdJsonParser *self, FwupdJsonParserHelper *helper, GError **error)
{
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	if (G_UNLIKELY(!fwupd_json_parser_helper_check_depth(self, ++helper->depth, error)))
		return NULL;
	while (TRUE) {
		g_autoptr(GRefString) str = NULL;
		FwupdJsonParserToken token = FWUPD_JSON_PARSER_TOKEN_INVALID;

		if (!fwupd_json_parser_helper_get_next_token(helper, &token, &str, error))
			return NULL;
		if (token == FWUPD_JSON_PARSER_TOKEN_ARRAY_END)
			break;
		if (token == FWUPD_JSON_PARSER_TOKEN_OBJECT_START) {
			g_autoptr(FwupdJsonObject) json_obj =
			    fwupd_json_parser_load_object(self, helper, error);
			if (G_UNLIKELY(json_obj == NULL))
				return NULL;
			fwupd_json_array_add_object(json_arr, json_obj);
		} else if (token == FWUPD_JSON_PARSER_TOKEN_ARRAY_START) {
			g_autoptr(FwupdJsonArray) json_array2 =
			    fwupd_json_parser_load_array(self, helper, error);
			if (G_UNLIKELY(json_array2 == NULL))
				return NULL;
			fwupd_json_array_add_array(json_arr, json_array2);
		} else if (token == FWUPD_JSON_PARSER_TOKEN_STRING) {
			fwupd_json_array_add_string_internal(json_arr, str);
		} else if (token == FWUPD_JSON_PARSER_TOKEN_RAW) {
			if (G_UNLIKELY(str == NULL)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "no raw data on line %u",
					    helper->linecnt);
				return NULL;
			}
			fwupd_json_array_add_raw_internal(json_arr, str);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "object delimiter not expected in array");
			return NULL;
		}
		if (G_UNLIKELY(self->max_items > 0 &&
			       fwupd_json_array_get_size(json_arr) > self->max_items)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many items in array, limit was %u",
				    self->max_items);
			return NULL;
		}
	}
	helper->depth--;

	/* success */
	return g_steal_pointer(&json_arr);
}

static FwupdJsonObject *
fwupd_json_parser_load_object(FwupdJsonParser *self, FwupdJsonParserHelper *helper, GError **error)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	if (!fwupd_json_parser_helper_check_depth(self, ++helper->depth, error))
		return NULL;
	while (TRUE) {
		FwupdJsonParserToken token1 = FWUPD_JSON_PARSER_TOKEN_INVALID;
		FwupdJsonParserToken token2 = FWUPD_JSON_PARSER_TOKEN_INVALID;
		FwupdJsonParserToken token3 = FWUPD_JSON_PARSER_TOKEN_INVALID;
		g_autoptr(GRefString) key = NULL;
		g_autoptr(GRefString) val = NULL;

		/* "key" : value */
		if (!fwupd_json_parser_helper_get_next_token(helper, &token1, &key, error))
			return NULL;
		if (token1 == FWUPD_JSON_PARSER_TOKEN_OBJECT_END)
			break;
		if (G_UNLIKELY(token1 != FWUPD_JSON_PARSER_TOKEN_STRING)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "object key '%s' must be quoted on line %u",
				    key,
				    helper->linecnt);
			return NULL;
		}
		if (!fwupd_json_parser_helper_get_next_token(helper, &token2, NULL, error))
			return NULL;
		if (token2 != FWUPD_JSON_PARSER_TOKEN_OBJECT_DELIM) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "did not find object delimiter on line %u",
				    helper->linecnt);
			return NULL;
		}
		if (!fwupd_json_parser_helper_get_next_token(helper, &token3, &val, error))
			return NULL;

		if (token3 == FWUPD_JSON_PARSER_TOKEN_OBJECT_START) {
			g_autoptr(FwupdJsonObject) json_obj2 =
			    fwupd_json_parser_load_object(self, helper, error);
			if (G_UNLIKELY(json_obj2 == NULL))
				return NULL;
			fwupd_json_object_add_object_internal(json_obj, key, json_obj2);
		} else if (token3 == FWUPD_JSON_PARSER_TOKEN_ARRAY_START) {
			g_autoptr(FwupdJsonArray) json_array2 =
			    fwupd_json_parser_load_array(self, helper, error);
			if (G_UNLIKELY(json_array2 == NULL))
				return NULL;
			fwupd_json_object_add_array_internal(json_obj, key, json_array2);
		} else if (token3 == FWUPD_JSON_PARSER_TOKEN_STRING) {
			fwupd_json_object_add_string_internal(json_obj, key, val, helper->flags);
		} else {
			if (G_UNLIKELY(val == NULL)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "did not find raw value on line %u",
					    helper->linecnt);
				return NULL;
			}
			fwupd_json_object_add_raw_internal(json_obj, key, val, helper->flags);
		}
		if (G_UNLIKELY(self->max_items > 0 &&
			       fwupd_json_object_get_size(json_obj) > self->max_items)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many items in object, limit was %u",
				    self->max_items);
			return NULL;
		}
	}
	helper->depth--;

	/* success */
	return g_steal_pointer(&json_obj);
}

static FwupdJsonNode *
fwupd_json_parser_load_from_stream_internal(FwupdJsonParser *self,
					    FwupdJsonParserHelper *helper,
					    GError **error)
{
	FwupdJsonParserToken token = FWUPD_JSON_PARSER_TOKEN_INVALID;
	g_autoptr(GRefString) str = NULL;

	if (!fwupd_json_parser_helper_get_next_token(helper, &token, &str, error))
		return NULL;
	if (token == FWUPD_JSON_PARSER_TOKEN_OBJECT_START) {
		g_autoptr(FwupdJsonObject) json_obj = NULL;
		json_obj = fwupd_json_parser_load_object(self, helper, error);
		if (json_obj == NULL)
			return NULL;
		return fwupd_json_node_new_object(json_obj);
	}
	if (token == FWUPD_JSON_PARSER_TOKEN_ARRAY_START) {
		g_autoptr(FwupdJsonArray) json_arr = NULL;
		json_arr = fwupd_json_parser_load_array(self, helper, error);
		if (json_arr == NULL)
			return NULL;
		return fwupd_json_node_new_array(json_arr);
	}
	if (token == FWUPD_JSON_PARSER_TOKEN_STRING)
		return fwupd_json_node_new_string_internal(str);
	if (token == FWUPD_JSON_PARSER_TOKEN_RAW)
		return fwupd_json_node_new_raw_internal(str);

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid JSON; token was not object, array, string or raw");
	return NULL;
}

/**
 * fwupd_json_parser_load_from_bytes: (skip):
 * @self: a #FwupdJsonParser
 * @blob: a #GBytes
 * @flags: a #FwupdJsonLoadFlags
 * @error: (nullable): optional return location for an error
 *
 * Loads JSON from a string.
 *
 * Returns: (transfer full): a #FwupdJsonNode, or %NULL for error
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_parser_load_from_bytes(FwupdJsonParser *self,
				  GBytes *blob,
				  FwupdJsonLoadFlags flags,
				  GError **error)
{
	g_autoptr(FwupdJsonParserHelper) helper = fwupd_json_parser_helper_new();

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	helper->flags = flags;
	helper->stream = g_memory_input_stream_new_from_bytes(blob);
	return fwupd_json_parser_load_from_stream_internal(self, helper, error);
}

/**
 * fwupd_json_parser_load_from_data: (skip):
 * @self: a #FwupdJsonParser
 * @text: a string
 * @flags: a #FwupdJsonLoadFlags
 * @error: (nullable): optional return location for an error
 *
 * Loads JSON from a string.
 *
 * Returns: (transfer full): a #FwupdJsonNode, or %NULL for error
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_parser_load_from_data(FwupdJsonParser *self,
				 const gchar *text,
				 FwupdJsonLoadFlags flags,
				 GError **error)
{
	g_autoptr(FwupdJsonParserHelper) helper = fwupd_json_parser_helper_new();

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(text != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	helper->flags = flags;
	helper->stream = g_memory_input_stream_new_from_data(text, strlen(text), NULL);
	return fwupd_json_parser_load_from_stream_internal(self, helper, error);
}

/**
 * fwupd_json_parser_load_from_stream: (skip):
 * @self: a #FwupdJsonParser
 * @stream: a #GInputStream
 * @flags: a #FwupdJsonLoadFlags
 * @error: (nullable): optional return location for an error
 *
 * Loads JSON from a stream.
 *
 * Returns: (transfer full): a #FwupdJsonNode, or %NULL for error
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_parser_load_from_stream(FwupdJsonParser *self,
				   GInputStream *stream,
				   FwupdJsonLoadFlags flags,
				   GError **error)
{
	g_autoptr(FwupdJsonParserHelper) helper = fwupd_json_parser_helper_new();

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* tokenize in chunks */
	if (!g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_SET, NULL, error)) {
		fwupd_error_convert(error);
		return NULL;
	}
	helper->stream = g_object_ref(stream);
	helper->flags = flags;
	return fwupd_json_parser_load_from_stream_internal(self, helper, error);
}

static void
fwupd_json_parser_class_init(FwupdJsonParserClass *klass)
{
}

static void
fwupd_json_parser_init(FwupdJsonParser *self)
{
}

/**
 * fwupd_json_parser_new:
 *
 * Returns: (transfer full): a #FwupdJsonParser
 *
 * Since: 2.1.1
 **/
FwupdJsonParser *
fwupd_json_parser_new(void)
{
	return g_object_new(FWUPD_TYPE_JSON_PARSER, NULL);
}
