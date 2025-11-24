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

struct _FwupdJsonParser {
	GObject parent_instance;
	guint max_depth;
};

G_DEFINE_TYPE(FwupdJsonParser, fwupd_json_parser, G_TYPE_OBJECT)

/**
 * fwupd_json_parser_set_max_depth:
 * @self: a #FwupdJsonParser
 * @max_depth: max nesting depth
 *
 * Sets the maximum nesting depth.
 *
 * Since: 2.1.0
 **/
void
fwupd_json_parser_set_max_depth(FwupdJsonParser *self, guint max_depth)
{
	g_return_if_fail(FWUPD_IS_JSON_PARSER(self));
	self->max_depth = max_depth;
}

typedef enum {
	FWUPD_JSON_PARSER_TOKEN_KIND_NONE,
	FWUPD_JSON_PARSER_TOKEN_KIND_QUOTED,
	FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_START,
	FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_END,
	FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_DELIM,
	FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_START,
	FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_END,
} FwupdJsonParserTokenKind;

static FwupdJsonParserTokenKind
fwupd_json_parser_token_kind_from_byte(gchar byte)
{
	if (byte == '{')
		return FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_START;
	if (byte == '}')
		return FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_END;
	if (byte == '[')
		return FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_START;
	if (byte == ']')
		return FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_END;
	if (byte == ':')
		return FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_DELIM;
	return FWUPD_JSON_PARSER_TOKEN_KIND_NONE;
}

typedef struct {
	FwupdJsonParserTokenKind kind;
	GRefString *str;
} FwupdJsonParserToken;

static FwupdJsonParserToken *
fwupd_json_parser_token_new(FwupdJsonParserTokenKind kind, const gchar *str)
{
	FwupdJsonParserToken *token = g_new0(FwupdJsonParserToken, 1);
	token->kind = kind;
	if (str != NULL)
		token->str = g_ref_string_new(str);
	return token;
}

static void
fwupd_json_parser_token_free(FwupdJsonParserToken *token)
{
	if (token->str != NULL)
		g_ref_string_release(token->str);
	g_free(token);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdJsonParserToken, fwupd_json_parser_token_free)

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
	helper->acc = g_string_new(NULL);
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

static FwupdJsonParserToken *
fwupd_json_parser_helper_dump_acc(FwupdJsonParserHelper *helper)
{
	FwupdJsonParserToken *token;

	if (helper->is_quoted) {
		token = fwupd_json_parser_token_new(FWUPD_JSON_PARSER_TOKEN_KIND_QUOTED,
						    helper->acc->str);
	} else {
		if (helper->acc->len == 0)
			return NULL;
		token = fwupd_json_parser_token_new(FWUPD_JSON_PARSER_TOKEN_KIND_NONE,
						    helper->acc->str);
	}
	g_string_truncate(helper->acc, 0);
	return token;
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
	if (data == '\\')
		return '\\';
	return 0;
}

static gboolean
fwupd_json_parser_helper_get_next_token_chunk(FwupdJsonParserHelper *helper,
					      FwupdJsonParserToken **token,
					      gboolean *buf_offset_enable,
					      GError **error)
{
	gchar data;

	/* need more data */
	if (helper->buf_offset >= helper->buf->len) {
		if (!fwupd_json_parser_helper_slurp(helper, error))
			return FALSE;
	}
	data = helper->buf->data[helper->buf_offset];

	/* quotes */
	if (data == '"') {
		if (helper->is_quoted) {
			*token = fwupd_json_parser_helper_dump_acc(helper);
			helper->is_quoted = FALSE;
			return TRUE;
		}
		helper->is_quoted = TRUE;
		return TRUE;
	}

	/* newline, for error messages */
	if (!helper->is_quoted && data == '\n') {
		helper->linecnt++;
		*token = fwupd_json_parser_helper_dump_acc(helper);
		return TRUE;
	}

	/* split */
	if (!helper->is_quoted && data == ',') {
		*token = fwupd_json_parser_helper_dump_acc(helper);
		return TRUE;
	}

	/* stack nocheck:lines */
	if (!helper->is_quoted) {
		FwupdJsonParserTokenKind token_kind = fwupd_json_parser_token_kind_from_byte(data);
		if (token_kind != FWUPD_JSON_PARSER_TOKEN_KIND_NONE) {
			*token = fwupd_json_parser_helper_dump_acc(helper);
			if (*token != NULL) {
				*buf_offset_enable = FALSE;
				return TRUE;
			}
			*token = fwupd_json_parser_token_new(token_kind, NULL);
			return TRUE;
		}
	}

	/* escape char */
	if (helper->is_quoted && !helper->is_escape && data == '\\') {
		helper->is_escape = TRUE;
		return TRUE;
	}
	if (helper->is_escape) {
		data = fwupd_json_parser_unescape_char(data);
		if (data == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid escape char '%c'",
				    data);
			return FALSE;
		}
		helper->is_escape = FALSE;
	}

	/* whitespace */
	if (!helper->is_quoted && g_ascii_isspace(data))
		return TRUE;

	/* save acc */
	g_string_append_c(helper->acc, data);
	return TRUE;
}

static FwupdJsonParserToken *
fwupd_json_parser_helper_get_next_token(FwupdJsonParserHelper *helper, GError **error)
{
	FwupdJsonParserToken *token = NULL;

	/* process each byte until we get a token */
	while (token == NULL) {
		gboolean buf_offset_enable = TRUE;
		if (!fwupd_json_parser_helper_get_next_token_chunk(helper,
								   &token,
								   &buf_offset_enable,
								   error))
			return NULL;
		if (buf_offset_enable)
			helper->buf_offset++;
	}
	return token;
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
	g_autoptr(FwupdJsonArray) json_array = fwupd_json_array_new();

	if (!fwupd_json_parser_helper_check_depth(self, ++helper->depth, error))
		return NULL;
	while (TRUE) {
		g_autoptr(FwupdJsonParserToken) token = NULL;

		token = fwupd_json_parser_helper_get_next_token(helper, error);
		if (token == NULL)
			return NULL;
		if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_END)
			break;
		if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_START) {
			g_autoptr(FwupdJsonObject) json_object =
			    fwupd_json_parser_load_object(self, helper, error);
			if (json_object == NULL)
				return NULL;
			fwupd_json_array_add_object(json_array, json_object);
		} else if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_START) {
			g_autoptr(FwupdJsonArray) json_array2 =
			    fwupd_json_parser_load_array(self, helper, error);
			if (json_array2 == NULL)
				return NULL;
			fwupd_json_array_add_array(json_array, json_array2);
		} else if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_QUOTED) {
			fwupd_json_array_add_string_internal(json_array, token->str);
		} else {
			fwupd_json_array_add_raw_internal(json_array, token->str);
		}
	}
	helper->depth--;

	/* success */
	return g_steal_pointer(&json_array);
}

static FwupdJsonObject *
fwupd_json_parser_load_object(FwupdJsonParser *self, FwupdJsonParserHelper *helper, GError **error)
{
	g_autoptr(FwupdJsonObject) json_object = fwupd_json_object_new();

	if (!fwupd_json_parser_helper_check_depth(self, ++helper->depth, error))
		return NULL;
	while (TRUE) {
		g_autoptr(FwupdJsonParserToken) token1 = NULL;
		g_autoptr(FwupdJsonParserToken) token2 = NULL;
		g_autoptr(FwupdJsonParserToken) token3 = NULL;

		/* "key" : value */
		token1 = fwupd_json_parser_helper_get_next_token(helper, error);
		if (token1 == NULL)
			return NULL;
		if (token1->kind == FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_END)
			break;
		if (token1->kind != FWUPD_JSON_PARSER_TOKEN_KIND_QUOTED) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "object key '%s' must be quoted on line %u",
				    token1->str,
				    helper->linecnt);
			return NULL;
		}
		token2 = fwupd_json_parser_helper_get_next_token(helper, error);
		if (token2 == NULL)
			return NULL;
		if (token2->kind != FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_DELIM) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "did not find object delimiter on line %u",
				    helper->linecnt);
			return NULL;
		}
		token3 = fwupd_json_parser_helper_get_next_token(helper, error);
		if (token3 == NULL)
			return NULL;

		if (token3->kind == FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_START) {
			g_autoptr(FwupdJsonObject) json_object2 =
			    fwupd_json_parser_load_object(self, helper, error);
			if (json_object2 == NULL)
				return NULL;
			fwupd_json_object_add_object_internal(json_object, token1->str, json_object2);
		} else if (token3->kind == FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_START) {
			g_autoptr(FwupdJsonArray) json_array2 =
			    fwupd_json_parser_load_array(self, helper, error);
			if (json_array2 == NULL)
				return NULL;
			fwupd_json_object_add_array_internal(json_object, token1->str, json_array2);
		} else if (token3->kind == FWUPD_JSON_PARSER_TOKEN_KIND_QUOTED) {
			if (token3->str == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "did not find string value on line %u",
					    helper->linecnt);
				return NULL;
			}
			fwupd_json_object_add_string_internal(json_object,
							      token1->str,
							      token3->str,
							      helper->flags);
		} else {
			if (token3->str == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "did not find raw value on line %u",
					    helper->linecnt);
				return NULL;
			}
			fwupd_json_object_add_raw_internal(json_object,
							   token1->str,
							   token3->str,
							   helper->flags);
		}
	}
	helper->depth--;

	/* success */
	return g_steal_pointer(&json_object);
}

static FwupdJsonNode *
fwupd_json_parser_load_from_stream_internal(FwupdJsonParser *self,
					    FwupdJsonParserHelper *helper,
					    GError **error)
{
	g_autoptr(FwupdJsonParserToken) token = NULL;

	token = fwupd_json_parser_helper_get_next_token(helper, error);
	if (token == NULL)
		return NULL;
	if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_OBJECT_START) {
		g_autoptr(FwupdJsonObject) json_object = NULL;
		json_object = fwupd_json_parser_load_object(self, helper, error);
		if (json_object == NULL)
			return NULL;
		return fwupd_json_node_new_object(json_object);
	}
	if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_ARRAY_START) {
		g_autoptr(FwupdJsonArray) json_array = NULL;
		json_array = fwupd_json_parser_load_array(self, helper, error);
		if (json_array == NULL)
			return NULL;
		return fwupd_json_node_new_array(json_array);
	}
	if (token->kind == FWUPD_JSON_PARSER_TOKEN_KIND_QUOTED)
		return fwupd_json_node_new_string_internal(token->str);
	return fwupd_json_node_new_raw_internal(token->str);
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
 * Since: 2.1.0
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
 * Since: 2.1.0
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
 * Since: 2.1.0
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
 * Since: 2.1.0
 **/
FwupdJsonParser *
fwupd_json_parser_new(void)
{
	return g_object_new(FWUPD_TYPE_JSON_PARSER, NULL);
}
