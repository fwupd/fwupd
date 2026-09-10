/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-json-node-private.h"
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
	FwupdRsJsonParser *rs;
};

G_DEFINE_TYPE(FwupdJsonParser, fwupd_json_parser, G_TYPE_OBJECT)

/**
 * fwupd_json_parser_set_max_depth:
 * @self: a #FwupdJsonParser
 * @max_depth: max nesting depth
 *
 * Sets the maximum nesting depth.
 *
 * The default maximum depth is %G_MAXUINT16, but users of #FwupdJsonParser should use this function
 * to set a better limit.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_parser_set_max_depth(FwupdJsonParser *self, guint max_depth)
{
	g_return_if_fail(FWUPD_IS_JSON_PARSER(self));
	fwupd_rs_json_parser_set_max_depth(self->rs, max_depth);
}

/**
 * fwupd_json_parser_set_max_items:
 * @self: a #FwupdJsonParser
 * @max_items: max items
 *
 * Sets the maximum number of items in an array or object.
 *
 * The default maximum items is %G_MAXUINT16, but users of #FwupdJsonParser should use this function
 * to set a better limit.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_parser_set_max_items(FwupdJsonParser *self, guint max_items)
{
	g_return_if_fail(FWUPD_IS_JSON_PARSER(self));
	fwupd_rs_json_parser_set_max_items(self->rs, max_items);
}

/**
 * fwupd_json_parser_set_max_quoted:
 * @self: a #FwupdJsonParser
 * @max_quoted: maximum size of a quoted string
 *
 * Sets the maximum size of a quoted string.
 *
 * The default maximum quoted string length is %G_MAXUINT16, but users of #FwupdJsonParser should
 * use this function to set a better limit.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_parser_set_max_quoted(FwupdJsonParser *self, guint max_quoted)
{
	g_return_if_fail(FWUPD_IS_JSON_PARSER(self));
	fwupd_rs_json_parser_set_max_quoted(self->rs, max_quoted);
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
	const guint8 *data;
	gsize data_len;
	FwupdRsJsonNode *rs;

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	data = g_bytes_get_data(blob, &data_len);
	rs = fwupd_rs_json_parser_load_from_bytes(self->rs, data, data_len, (guint)flags, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
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
	FwupdRsJsonNode *rs;

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(text != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_parser_load_from_data(self->rs, text, (guint)flags, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
}

static gssize
fwupd_json_parser_stream_read_cb(void *handle,
				 guint8 *buf,
				 gsize count,
				 GError **error) /* nocheck:name */
{
	return g_input_stream_read(G_INPUT_STREAM(handle), /* nocheck:blocked */
				   buf,
				   count,
				   NULL,
				   error);
}

static gboolean
fwupd_json_parser_stream_seek_cb(void *handle,
				 gint64 offset,
				 gint32 type,
				 GError **error) /* nocheck:name */
{
	if (!G_IS_SEEKABLE(handle)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "stream is not seekable"); /* nocheck:error */
		return FALSE;
	}
	return g_seekable_seek(G_SEEKABLE(handle), offset, (GSeekType)type, NULL, error);
}

static gboolean
fwupd_json_parser_stream_can_seek_cb(void *handle) /* nocheck:name */
{
	if (!G_IS_SEEKABLE(handle))
		return FALSE;
	return g_seekable_can_seek(G_SEEKABLE(handle));
}

static gint64
fwupd_json_parser_stream_tell_cb(void *handle) /* nocheck:name */
{
	if (!G_IS_SEEKABLE(handle))
		return 0;
	return g_seekable_tell(G_SEEKABLE(handle));
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
	FwupdRsJsonNode *rs;
	FuRsStreamCallbacks callbacks = {
	    .read_fn = fwupd_json_parser_stream_read_cb,
	    .seek_fn = fwupd_json_parser_stream_seek_cb,
	    .can_seek_fn = fwupd_json_parser_stream_can_seek_cb,
	    .tell_fn = fwupd_json_parser_stream_tell_cb,
	    .destroy_fn = NULL,
	};

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs =
	    fwupd_rs_json_parser_load_from_stream(self->rs, stream, callbacks, (guint)flags, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
}

/**
 * fwupd_json_parser_load_from_stream_impl: (skip):
 * @self: a #FwupdJsonParser
 * @stream_impl: (transfer full): an opaque Rust stream handle
 * @flags: a #FwupdJsonLoadFlags
 * @error: (nullable): optional return location for an error
 *
 * Loads JSON from a Rust stream implementation handle. This is an internal
 * function used by libfwupdplugin to avoid converting streams to GInputStream.
 *
 * Returns: (transfer full): a #FwupdJsonNode, or %NULL for error
 *
 * Since: 2.1.8
 **/
FwupdJsonNode *
fwupd_json_parser_load_from_stream_impl(FwupdJsonParser *self,
					FwupdStreamImpl stream_impl,
					FwupdJsonLoadFlags flags,
					GError **error)
{
	FwupdRsJsonNode *rs;

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(stream_impl != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* ownership of stream_impl is transferred */
	rs = fwupd_rs_json_parser_load_from_stream_impl(self->rs, stream_impl, (guint)flags, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
}

static void
fwupd_json_parser_finalize(GObject *object)
{
	FwupdJsonParser *self = FWUPD_JSON_PARSER(object);
	fwupd_rs_json_parser_free(self->rs);
	G_OBJECT_CLASS(fwupd_json_parser_parent_class)->finalize(object);
}

static void
fwupd_json_parser_class_init(FwupdJsonParserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_json_parser_finalize;
}

static void
fwupd_json_parser_init(FwupdJsonParser *self)
{
	self->rs = fwupd_rs_json_parser_new();
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
