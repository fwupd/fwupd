/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-error.h"
#include "fwupd-jcat-file.h"
#include "fwupd-json-array.h"
#include "fwupd-json-parser.h"

struct _FwupdJcatFile {
	GObject parent_instance;
	GPtrArray *items;
	guint32 version_major;
	guint32 version_minor;
};

static void
fwupd_jcat_file_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdJcatFile,
		       fwupd_jcat_file,
		       G_TYPE_OBJECT,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_jcat_file_codec_iface_init));

static void
fwupd_jcat_file_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FwupdJcatFile *self = FWUPD_JCAT_FILE(codec);

	if (self->version_major > 0 || self->version_minor > 0) {
		g_autofree gchar *version = NULL;
		version = g_strdup_printf("%u.%u", self->version_major, self->version_minor);
		fwupd_codec_string_append(str, idt, "Version", version);
	}
	for (guint i = 0; i < self->items->len; i++) {
		FwupdJcatItem *item = g_ptr_array_index(self->items, i);
		fwupd_codec_add_string(FWUPD_CODEC(item), idt, str);
	}
}

static gboolean
fwupd_jcat_file_import_node(FwupdJcatFile *self,
			    FwupdJsonNode *json_node,
			    GError **error)
{
	gint64 version = 0;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonArray) json_array = NULL;

	/* get version */
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;
	if (!fwupd_json_object_get_integer(json_obj, "JcatVersionMajor", &version, error))
		return FALSE;
	if (version < 0 || version > G_MAXUINT32) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid major version");
		return FALSE;
	}
	self->version_major = (guint32)version;
	if (!fwupd_json_object_get_integer(json_obj, "JcatVersionMinor", &version, error))
		return FALSE;
	if (version < 0 || version > G_MAXUINT32) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid minor version");
		return FALSE;
	}
	self->version_minor = (guint32)version;

	/* get items */
	json_array = fwupd_json_object_get_array(json_obj, "Items", error);
	if (json_array == NULL)
		return FALSE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_array); i++) {
		g_autoptr(FwupdJsonObject) json_item = NULL;
		g_autoptr(FwupdJcatItem) item = g_object_new(FWUPD_TYPE_JCAT_ITEM, NULL);

		json_item = fwupd_json_array_get_object(json_array, i, error);
		if (json_item == NULL)
			return FALSE;
		if (!fwupd_codec_from_json(FWUPD_CODEC(item), json_item, error))
			return FALSE;
		fwupd_jcat_file_add_item(self, item);
	}

	/* success */
	return TRUE;
}

static FwupdJsonObject *
fwupd_jcat_file_export_builder(FwupdJcatFile *self, FwupdCodecFlags flags)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_array = fwupd_json_array_new();

	/* add metadata */
	fwupd_json_object_add_integer(json_obj, "JcatVersionMajor", self->version_major);
	fwupd_json_object_add_integer(json_obj, "JcatVersionMinor", self->version_minor);

	/* add items */
	for (guint i = 0; i < self->items->len; i++) {
		FwupdJcatItem *item = g_ptr_array_index(self->items, i);
		g_autoptr(FwupdJsonObject) json_item = fwupd_json_object_new();
		fwupd_codec_to_json(FWUPD_CODEC(item), json_item, flags);
		fwupd_json_array_add_object(json_array, json_item);
	}
	fwupd_json_object_add_array(json_obj, "Items", json_array);

	/* success */
	return g_steal_pointer(&json_obj);
}

static FwupdJsonParser *
fwupd_jcat_file_json_parser_new(void)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	fwupd_json_parser_set_max_depth(json_parser, 5);
	fwupd_json_parser_set_max_items(json_parser, 100);
	fwupd_json_parser_set_max_quoted(json_parser, 100 * 1024);
	return g_steal_pointer(&json_parser);
}

/**
 * fwupd_jcat_file_import_json:
 * @self: #FwupdJcatFile
 * @json: (not nullable): JSON data
 * @error: #GError, or %NULL
 *
 * Imports a FwupdJcat file from raw JSON.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.1.3
 **/
gboolean
fwupd_jcat_file_import_json(FwupdJcatFile *self,
			    const gchar *json,
			    GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_jcat_file_json_parser_new();

	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), FALSE);
	g_return_val_if_fail(json != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	json_node =
	    fwupd_json_parser_load_from_data(json_parser, json, FWUPD_JSON_LOAD_FLAG_NONE, error);
	if (json_node == NULL)
		return FALSE;
	return fwupd_jcat_file_import_node(self, json_node, error);
}

/**
 * fwupd_jcat_file_import_stream:
 * @self: #FwupdJcatFile
 * @istream: (not nullable): #GInputStream
 * @error: #GError, or %NULL
 *
 * Imports a compressed FwupdJcat file from a file.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.1.3
 **/
gboolean
fwupd_jcat_file_import_stream(FwupdJcatFile *self,
			      GInputStream *istream,
			      GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_jcat_file_json_parser_new();
	g_autoptr(GConverter) conv = NULL;
	g_autoptr(GInputStream) istream_uncompressed = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(istream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP));
	istream_uncompressed = g_converter_input_stream_new(istream, conv);
	g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(istream_uncompressed),
						    FALSE);
	json_node = fwupd_json_parser_load_from_stream(json_parser,
						       istream_uncompressed,
						       FWUPD_JSON_LOAD_FLAG_NONE,
						       error);
	if (json_node == NULL)
		return FALSE;
	return fwupd_jcat_file_import_node(self, json_node, error);
}

/**
 * fwupd_jcat_file_import_bytes:
 * @self: #FwupdJcatFile
 * @blob: (not nullable): a #GBytes
 * @error: #GError, or %NULL
 *
 * Imports a compressed FwupdJcat file from a blob of data.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.1.3
 **/
gboolean
fwupd_jcat_file_import_bytes(FwupdJcatFile *self,
			     GBytes *blob,
			     GError **error)
{
	g_autoptr(GInputStream) istream = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), FALSE);
	g_return_val_if_fail(blob != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	istream = g_memory_input_stream_new_from_bytes(blob);
	return fwupd_jcat_file_import_stream(self, istream, error);
}

/**
 * fwupd_jcat_file_export_json:
 * @self: #FwupdJcatFile
 * @flags: a #FwupdCodecFlags, typically %FWUPD_CODEC_FLAG_NONE
 * @error: #GError, or %NULL
 *
 * Exports a FwupdJcat file to raw JSON.
 *
 * Returns: (transfer full): JSON output, or %NULL for error
 *
 * Since: 2.1.3
 **/
gchar *
fwupd_jcat_file_export_json(FwupdJcatFile *self, FwupdCodecFlags flags, GError **error)
{
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(GString) str = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_obj = fwupd_jcat_file_export_builder(self, flags);
	str = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fwupd_jcat_file_export_bytes:
 * @self: #FwupdJcatFile
 * @error: #GError, or %NULL
 *
 * Exports a FwupdJcat file to a compressed blob.
 *
 * Returns: (transfer full): a #GBytes
 *
 * Since: 2.1.3
 **/
GBytes *
fwupd_jcat_file_export_bytes(FwupdJcatFile *self, GError **error)
{
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(GConverter) converter = NULL;
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* export all */
	json_obj = fwupd_jcat_file_export_builder(self, FWUPD_CODEC_FLAG_NONE);
	blob = fwupd_json_object_to_bytes(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);

	/* compress blob */
	converter = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP, -1));
#if GLIB_CHECK_VERSION(2, 82, 0)
	return g_converter_convert_bytes(converter, blob, error);
#else
	{
		guint8 tmp[0x8000]; /* nocheck:zero-init */
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;
		g_autoptr(GByteArray) buf = g_byte_array_new();
		g_autoptr(GError) error_local = NULL;

		istream1 = g_memory_input_stream_new_from_bytes(blob);
		istream2 = g_converter_input_stream_new(istream1, converter);
		while (TRUE) {
			gssize sz;
			sz = g_input_stream_read(istream2, tmp, sizeof(tmp), NULL, &error_local);
			if (sz == 0)
				break;
			if (sz < 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    error_local->message);
				return NULL;
			}
			g_byte_array_append(buf, tmp, sz);
		}
		return g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
	}
#endif
}

/**
 * fwupd_jcat_file_get_items:
 * @self: #FwupdJcatFile
 *
 * Returns all the items in the file.
 *
 * Returns: (transfer container) (element-type FwupdJcatItem): all the items in the file
 *
 * Since: 2.1.3
 **/
GPtrArray *
fwupd_jcat_file_get_items(FwupdJcatFile *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), NULL);
	return g_ptr_array_ref(self->items);
}

/**
 * fwupd_jcat_file_get_item_by_id:
 * @self: #FwupdJcatFile
 * @id: (not nullable): An ID, typically a filename basename
 * @error: #GError, or %NULL
 *
 * Finds the item with the specified ID, falling back to the ID alias if set.
 *
 * Returns: (transfer full): a #FwupdJcatItem, or %NULL if the filename was not found
 *
 * Since: 2.1.3
 **/
FwupdJcatItem *
fwupd_jcat_file_get_item_by_id(FwupdJcatFile *self, const gchar *id, GError **error)
{
	g_autoptr(FwupdJcatItem) item_found = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), NULL);
	g_return_val_if_fail(id != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* exact ID match */
	for (guint i = 0; i < self->items->len; i++) {
		FwupdJcatItem *item = g_ptr_array_index(self->items, i);
		if (g_strcmp0(fwupd_jcat_item_get_id(item), id) == 0) {
			if (item_found != NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "multiple matches for %s",
					    id);
				return NULL;
			}
			item_found = g_object_ref(item);
		}
	}
	if (item_found != NULL)
		return g_steal_pointer(&item_found);

	/* try aliases this time */
	for (guint i = 0; i < self->items->len; i++) {
		FwupdJcatItem *item = g_ptr_array_index(self->items, i);
		g_autoptr(GPtrArray) alias_ids = fwupd_jcat_item_get_alias_ids(item);
		for (guint j = 0; j < alias_ids->len; j++) {
			const gchar *id_tmp = g_ptr_array_index(alias_ids, j);
			if (g_strcmp0(id_tmp, id) == 0) {
				if (item_found != NULL) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "multiple aliases for %s",
						    id);
					return NULL;
				}
				item_found = g_object_ref(item);
			}
		}
	}
	if (item_found != NULL)
		return g_steal_pointer(&item_found);

	/* failed */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "failed to find %s", id);
	return NULL;
}

/**
 * fwupd_jcat_file_get_item_default:
 * @self: #FwupdJcatFile
 * @error: #GError, or %NULL
 *
 * Finds the default item. If more than one #FwupdJcatItem exists this function will
 * return with an error.
 *
 * Returns: (transfer full): a #FwupdJcatItem, or %NULL if no default exists
 *
 * Since: 2.1.3
 **/
FwupdJcatItem *
fwupd_jcat_file_get_item_default(FwupdJcatFile *self, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (self->items->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no items found");
		return NULL;
	}
	if (self->items->len > 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "multiple items found, no default possible");
		return NULL;
	}

	/* only one possible */
	return g_object_ref(g_ptr_array_index(self->items, 0));
}

/**
 * fwupd_jcat_file_add_item:
 * @self: #FwupdJcatFile
 * @item: (not nullable): #FwupdJcatItem
 *
 * Adds an item to a file.
 *
 * Since: 2.1.3
 **/
void
fwupd_jcat_file_add_item(FwupdJcatFile *self, FwupdJcatItem *item)
{
	g_return_if_fail(FWUPD_IS_JCAT_FILE(self));
	g_return_if_fail(FWUPD_IS_JCAT_ITEM(item));
	g_ptr_array_add(self->items, g_object_ref(item));
}

/**
 * fwupd_jcat_file_get_version_major:
 * @self: #FwupdJcatFile
 *
 * Returns the major version number of the FwupdJcat specification
 *
 * Returns: integer
 *
 * Since: 2.1.3
 **/
guint32
fwupd_jcat_file_get_version_major(FwupdJcatFile *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), 0);
	return self->version_major;
}

/**
 * fwupd_jcat_file_get_version_minor:
 * @self: #FwupdJcatFile
 *
 * Returns the minor version number of the FwupdJcat specification
 *
 * Returns: integer
 *
 * Since: 2.1.3
 **/
guint32
fwupd_jcat_file_get_version_minor(FwupdJcatFile *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_FILE(self), 0);
	return self->version_minor;
}

static void
fwupd_jcat_file_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fwupd_jcat_file_add_string;
}

static void
fwupd_jcat_file_finalize(GObject *obj)
{
	FwupdJcatFile *self = FWUPD_JCAT_FILE(obj);

	g_ptr_array_unref(self->items);
	G_OBJECT_CLASS(fwupd_jcat_file_parent_class)->finalize(obj);
}

static void
fwupd_jcat_file_class_init(FwupdJcatFileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_jcat_file_finalize;
}

static void
fwupd_jcat_file_init(FwupdJcatFile *self)
{
	self->items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->version_major = 0;
	self->version_minor = 1;
}

/**
 * fwupd_jcat_file_new:
 *
 * Creates a new file.
 *
 * Returns: a #FwupdJcatFile
 *
 * Since: 2.1.3
 **/
FwupdJcatFile *
fwupd_jcat_file_new(void)
{
	return g_object_new(FWUPD_TYPE_JCAT_FILE, NULL);
}
