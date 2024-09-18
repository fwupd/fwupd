/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-error.h"

/**
 * FwupdCodec:
 *
 * A codec that can serialize and deserialize objects to formats such as text, JSON or #GVariant.
 */

G_DEFINE_INTERFACE(FwupdCodec, fwupd_codec, G_TYPE_OBJECT)

static void
fwupd_codec_default_init(FwupdCodecInterface *iface)
{
}

static void
fwupd_codec_add_string_from_json_node(FwupdCodec *self,
				      const gchar *member_name,
				      JsonNode *json_node,
				      guint idt,
				      GString *str)
{
	JsonNodeType node_type = json_node_get_node_type(json_node);
	if (node_type == JSON_NODE_VALUE) {
		GType gtype = json_node_get_value_type(json_node);
		if (gtype == G_TYPE_STRING) {
			fwupd_codec_string_append(str,
						  idt,
						  member_name,
						  json_node_get_string(json_node));
		} else if (gtype == G_TYPE_INT64) {
			fwupd_codec_string_append_hex(str,
						      idt,
						      member_name,
						      json_node_get_int(json_node));
		} else if (gtype == G_TYPE_BOOLEAN) {
			fwupd_codec_string_append_bool(str,
						       idt,
						       member_name,
						       json_node_get_boolean(json_node));
		} else {
			fwupd_codec_string_append(str, idt, member_name, "GType value unknown");
		}
	} else if (node_type == JSON_NODE_ARRAY) {
		JsonArray *json_array = json_node_get_array(json_node);
		g_autoptr(GList) json_nodes = json_array_get_elements(json_array);
		if (g_strcmp0(member_name, "") != 0)
			fwupd_codec_string_append(str, idt, member_name, "");
		for (GList *l = json_nodes; l != NULL; l = l->next)
			fwupd_codec_add_string_from_json_node(self, "", l->data, idt + 1, str);
	} else if (node_type == JSON_NODE_OBJECT) {
		JsonObjectIter iter;
		json_object_iter_init(&iter, json_node_get_object(json_node));
		if (g_strcmp0(member_name, "") != 0)
			fwupd_codec_string_append(str, idt, member_name, "");
		while (json_object_iter_next(&iter, &member_name, &json_node)) {
			fwupd_codec_add_string_from_json_node(self,
							      member_name,
							      json_node,
							      idt + 1,
							      str);
		}
	}
}

/**
 * fwupd_codec_add_string:
 * @self: a #FwupdCodec
 * @idt: the indent
 * @str: (not nullable): a string to append to
 *
 * Converts an object that implements #FwupdCodec to a debug string, appending it to @str.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_add_string(FwupdCodec *self, guint idt, GString *str)
{
	FwupdCodecInterface *iface;

	g_return_if_fail(FWUPD_IS_CODEC(self));
	g_return_if_fail(str != NULL);

	fwupd_codec_string_append(str, idt, G_OBJECT_TYPE_NAME(self), "");
	iface = FWUPD_CODEC_GET_IFACE(self);
	if (iface->add_string != NULL) {
		iface->add_string(self, idt + 1, str);
		return;
	}
	if (iface->add_json != NULL) {
		g_autoptr(JsonBuilder) builder = json_builder_new();
		g_autoptr(JsonNode) root_node = NULL;
		json_builder_begin_object(builder);
		iface->add_json(self, builder, FWUPD_CODEC_FLAG_TRUSTED);
		json_builder_end_object(builder);
		root_node = json_builder_get_root(builder);
		fwupd_codec_add_string_from_json_node(self, "", root_node, idt + 1, str);
		return;
	}
	g_critical("FwupdCodec->add_string or iface->add_json not implemented");
}

/**
 * fwupd_codec_to_string:
 * @self: a #FwupdCodec
 *
 * Converts an object that implements #FwupdCodec to a debug string.
 *
 * Returns: (transfer full): a string
 *
 * Since: 2.0.0
 */
gchar *
fwupd_codec_to_string(FwupdCodec *self)
{
	FwupdCodecInterface *iface;

	g_return_val_if_fail(FWUPD_IS_CODEC(self), NULL);

	iface = FWUPD_CODEC_GET_IFACE(self);
	if (iface->to_string != NULL)
		iface->to_string(self);
	if (iface->add_string != NULL || iface->add_json != NULL) {
		GString *str = g_string_new(NULL);
		fwupd_codec_add_string(self, 0, str);
		return g_string_free(str, FALSE);
	}
	g_critical("FwupdCodec->to_string and iface->add_string not implemented");
	return NULL;
}

/**
 * fwupd_codec_from_json:
 * @self: a #FwupdCodec
 * @json_node: (not nullable): a JSON node
 * @error: (nullable): optional return location for an error
 *
 * Converts an object that implements #FwupdCodec from a JSON object.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 */
gboolean
fwupd_codec_from_json(FwupdCodec *self, JsonNode *json_node, GError **error)
{
	FwupdCodecInterface *iface;

	g_return_val_if_fail(FWUPD_IS_CODEC(self), FALSE);
	g_return_val_if_fail(json_node != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	iface = FWUPD_CODEC_GET_IFACE(self);
	if (iface->from_json == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "FwupdCodec->from_json not implemented");
		return FALSE;
	}
	return (*iface->from_json)(self, json_node, error);
}

/**
 * fwupd_codec_from_json_string:
 * @self: a #FwupdCodec
 * @json: (not nullable): JSON text
 * @error: (nullable): optional return location for an error
 *
 * Converts an object that implements #FwupdCodec from a JSON string.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 */
gboolean
fwupd_codec_from_json_string(FwupdCodec *self, const gchar *json, GError **error)
{
	g_autoptr(JsonParser) parser = json_parser_new();

	g_return_val_if_fail(FWUPD_IS_CODEC(self), FALSE);
	g_return_val_if_fail(json != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!json_parser_load_from_data(parser, json, -1, error)) {
		g_prefix_error(error, "failed to load '%s': ", json);
		return FALSE;
	}
	return fwupd_codec_from_json(self, json_parser_get_root(parser), error);
}

/**
 * fwupd_codec_to_json:
 * @self: a #FwupdCodec
 * @builder: (not nullable): a JSON builder
 * @flags: a #FwupdCodecFlags, e.g. %FWUPD_CODEC_FLAG_TRUSTED
 *
 * Converts an object that implements #FwupdCodec to a JSON builder object.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_to_json(FwupdCodec *self, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FwupdCodecInterface *iface;

	g_return_if_fail(FWUPD_IS_CODEC(self));
	g_return_if_fail(builder != NULL);

	iface = FWUPD_CODEC_GET_IFACE(self);
	if (iface->add_json == NULL) {
		g_critical("FwupdCodec->add_json not implemented");
		return;
	}
	iface->add_json(self, builder, flags);
}

/**
 * fwupd_codec_to_json_string:
 * @self: a #FwupdCodec
 * @flags: a #FwupdCodecFlags, e.g. %FWUPD_CODEC_FLAG_TRUSTED
 * @error: (nullable): optional return location for an error
 *
 * Converts an object that implements #FwupdCodec to a JSON string.
 *
 * Returns: (transfer full): a string
 *
 * Since: 2.0.0
 */
gchar *
fwupd_codec_to_json_string(FwupdCodec *self, FwupdCodecFlags flags, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(FWUPD_IS_CODEC(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_builder_begin_object(builder);
	fwupd_codec_to_json(self, builder, flags);
	json_builder_end_object(builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed to convert to json");
		return NULL;
	}
	return g_steal_pointer(&data);
}

/**
 * fwupd_codec_from_variant:
 * @self: a #FwupdCodec
 * @value: (not nullable): a JSON node
 * @error: (nullable): optional return location for an error
 *
 * Converts an object that implements #FwupdCodec from a #GVariant value.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 */
gboolean
fwupd_codec_from_variant(FwupdCodec *self, GVariant *value, GError **error)
{
	FwupdCodecInterface *iface;

	g_return_val_if_fail(FWUPD_IS_CODEC(self), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	iface = FWUPD_CODEC_GET_IFACE(self);
	if (iface->from_variant != NULL)
		return (*iface->from_variant)(self, value, error);
	if (iface->from_variant_iter != NULL) {
		const gchar *type_string;
		type_string = g_variant_get_type_string(value);
		if (g_strcmp0(type_string, "(a{sv})") == 0) {
			g_autoptr(GVariantIter) iter = NULL;
			g_variant_get(value, "(a{sv})", &iter);
			iface->from_variant_iter(self, iter);
		} else if (g_strcmp0(type_string, "a{sv}") == 0) {
			g_autoptr(GVariantIter) iter = NULL;
			g_variant_get(value, "a{sv}", &iter);
			iface->from_variant_iter(self, iter);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "GVariant type %s not known",
				    type_string);
			return FALSE;
		}
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "FwupdCodec->from_variant not implemented");
	return FALSE;
}

/**
 * fwupd_codec_array_from_variant:
 * @value: a JSON node
 * @gtype: a #GType that implements `FwupdCodec`
 * @error: (nullable): optional return location for an error
 *
 * Converts an array of objects, each deserialized from a #GVariant value.
 *
 * Returns: (element-type GObject) (transfer container): %TRUE on success
 *
 * Since: 2.0.0
 */
GPtrArray *
fwupd_codec_array_from_variant(GVariant *value, GType gtype, GError **error)
{
	gsize sz;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) untuple = NULL;

	g_return_val_if_fail(value != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		g_autoptr(GObject) gobj = g_object_new(gtype, NULL);
		g_autoptr(GVariant) data = g_variant_get_child_value(untuple, i);
		if (!fwupd_codec_from_variant(FWUPD_CODEC(gobj), data, error))
			return NULL;
		g_ptr_array_add(array, g_steal_pointer(&gobj));
	}
	return g_steal_pointer(&array);
}

/**
 * fwupd_codec_array_to_json:
 * @array: (element-type GObject): (not nullable): array of objects that much implement `FwupdCodec`
 * @member_name: (not nullable): member name of the array
 * @builder: (not nullable): a #JsonBuilder
 * @flags: a #FwupdCodecFlags, e.g. %FWUPD_CODEC_FLAG_TRUSTED
 *
 * Converts an array of objects into a #GVariant value.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_array_to_json(GPtrArray *array,
			  const gchar *member_name,
			  JsonBuilder *builder,
			  FwupdCodecFlags flags)
{
	g_return_if_fail(array != NULL);
	g_return_if_fail(member_name != NULL);
	g_return_if_fail(JSON_IS_BUILDER(builder));

	json_builder_set_member_name(builder, member_name);
	json_builder_begin_array(builder);
	for (guint i = 0; i < array->len; i++) {
		FwupdCodec *codec = FWUPD_CODEC(g_ptr_array_index(array, i));
		json_builder_begin_object(builder);
		fwupd_codec_to_json(codec, builder, flags);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
}

/**
 * fwupd_codec_array_to_variant:
 * @array: (element-type GObject): (not nullable): array of objects that much implement `FwupdCodec`
 * @flags: a #FwupdCodecFlags, e.g. %FWUPD_CODEC_FLAG_TRUSTED
 *
 * Converts an array of objects into a #GVariant value.
 *
 * Returns: (transfer full): a #GVariant
 *
 * Since: 2.0.0
 */
GVariant *
fwupd_codec_array_to_variant(GPtrArray *array, FwupdCodecFlags flags)
{
	GVariantBuilder builder;

	g_return_val_if_fail(array != NULL, NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
	for (guint i = 0; i < array->len; i++) {
		FwupdCodec *codec = FWUPD_CODEC(g_ptr_array_index(array, i));
		g_variant_builder_add_value(&builder, fwupd_codec_to_variant(codec, flags));
	}
	return g_variant_new("(aa{sv})", &builder);
}

/**
 * fwupd_codec_to_variant:
 * @self: a #FwupdCodec
 * @flags: a #FwupdCodecFlags, e.g. %FWUPD_CODEC_FLAG_TRUSTED
 *
 * Converts an object that implements #FwupdCodec to a #GVariant.
 *
 * Returns: (transfer full): a #GVariant
 *
 * Since: 2.0.0
 */
GVariant *
fwupd_codec_to_variant(FwupdCodec *self, FwupdCodecFlags flags)
{
	FwupdCodecInterface *iface;

	g_return_val_if_fail(FWUPD_IS_CODEC(self), NULL);

	iface = FWUPD_CODEC_GET_IFACE(self);
	if (iface->to_variant != NULL)
		return iface->to_variant(self, flags);
	if (iface->add_variant != NULL) {
		GVariantBuilder builder;
		g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
		iface->add_variant(self, &builder, flags);
		return g_variant_new("a{sv}", &builder);
	}

	g_critical("FwupdCodec->add_variant and iface->add_variant not implemented");
	return NULL;
}

static gsize
_fu_strwidth(const gchar *text)
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
 * fwupd_codec_string_append:
 * @str: (not nullable): a #GString
 * @idt: the indent
 * @key: (not nullable): a string to append
 * @value: a string to append
 *
 * Appends a key and value to a string.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_string_append(GString *str, guint idt, const gchar *key, const gchar *value)
{
	const guint align = 24;
	gsize keysz;

	g_return_if_fail(str != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(idt * 2 < align);

	/* ignore */
	if (value == NULL)
		return;
	for (gsize i = 0; i < idt; i++)
		g_string_append(str, "  ");
	if (key[0] != '\0') {
		g_string_append_printf(str, "%s:", key);
		keysz = (idt * 2) + _fu_strwidth(key) + 1;
	} else {
		keysz = idt * 2;
	}
	if (value != NULL) {
		g_auto(GStrv) split = NULL;
		split = g_strsplit(value, "\n", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			if (i == 0) {
				g_string_append(str, " ");
				for (gsize j = keysz + 1; j < align; j++)
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
 * fwupd_codec_string_append_int:
 * @str: (not nullable): a #GString
 * @idt: the indent
 * @key: (not nullable): a string to append
 * @value: guint64
 *
 * Appends a key and unsigned integer to a string.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_string_append_int(GString *str, guint idt, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;

	g_return_if_fail(str != NULL);
	g_return_if_fail(key != NULL);

	/* ignore */
	if (value == 0)
		return;
	tmp = g_strdup_printf("%" G_GUINT64_FORMAT, value);
	fwupd_codec_string_append(str, idt, key, tmp);
}

/**
 * fwupd_codec_string_append_hex:
 * @str: (not nullable): a #GString
 * @idt: the indent
 * @key: (not nullable): a string to append
 * @value: guint64
 *
 * Appends a key and hex integer to a string.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_string_append_hex(GString *str, guint idt, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;

	g_return_if_fail(str != NULL);
	g_return_if_fail(key != NULL);

	/* ignore */
	if (value == 0)
		return;
	tmp = g_strdup_printf("0x%x", (guint)value);
	fwupd_codec_string_append(str, idt, key, tmp);
}

/**
 * fwupd_codec_string_append_bool:
 * @str: (not nullable): a #GString
 * @idt: the indent
 * @key: (not nullable): a string to append
 * @value: Boolean
 *
 * Appends a key and boolean value to a string.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_string_append_bool(GString *str, guint idt, const gchar *key, gboolean value)
{
	g_return_if_fail(str != NULL);
	g_return_if_fail(key != NULL);
	fwupd_codec_string_append(str, idt, key, value ? "true" : "false");
}

/**
 * fwupd_codec_string_append_time:
 * @str: (not nullable): a #GString
 * @idt: the indent
 * @key: (not nullable): a string to append
 * @value: guint64 UNIX time
 *
 * Appends a key and time value to a string.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_string_append_time(GString *str, guint idt, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(GDateTime) date = NULL;

	g_return_if_fail(str != NULL);
	g_return_if_fail(key != NULL);

	/* ignore */
	if (value == 0)
		return;

	date = g_date_time_new_from_unix_utc((gint64)value);
	tmp = g_date_time_format(date, "%F");
	fwupd_codec_string_append(str, idt, key, tmp);
}

/**
 * fwupd_codec_string_append_size:
 * @str: (not nullable): a #GString
 * @idt: the indent
 * @key: (not nullable): a string to append
 * @value: guint64
 *
 * Appends a key and size in bytes to a string.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_string_append_size(GString *str, guint idt, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;

	g_return_if_fail(str != NULL);
	g_return_if_fail(key != NULL);

	/* ignore */
	if (value == 0)
		return;
	tmp = g_format_size(value);
	fwupd_codec_string_append(str, idt, key, tmp);
}

/**
 * fwupd_codec_json_append:
 * @builder: (not nullable): a #JsonBuilder
 * @key: (not nullable): a string
 * @value: a string to append
 *
 * Appends a key and value to a JSON builder.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_json_append(JsonBuilder *builder, const gchar *key, const gchar *value)
{
	g_return_if_fail(JSON_IS_BUILDER(builder));
	g_return_if_fail(key != NULL);
	if (value == NULL)
		return;
	json_builder_set_member_name(builder, key);
	json_builder_add_string_value(builder, value);
}

/**
 * fwupd_codec_json_append_int:
 * @builder: (not nullable): a #JsonBuilder
 * @key: (not nullable): a string
 * @value: guint64
 *
 * Appends a key and unsigned integer to a JSON builder.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_json_append_int(JsonBuilder *builder, const gchar *key, guint64 value)
{
	g_return_if_fail(JSON_IS_BUILDER(builder));
	g_return_if_fail(key != NULL);
	json_builder_set_member_name(builder, key);
	json_builder_add_int_value(builder, value);
}

/**
 * fwupd_codec_json_append_bool:
 * @builder: (not nullable): a #JsonBuilder
 * @key: (not nullable): a string
 * @value: boolean
 *
 * Appends a key and boolean value to a JSON builder.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_json_append_bool(JsonBuilder *builder, const gchar *key, gboolean value)
{
	g_return_if_fail(JSON_IS_BUILDER(builder));
	g_return_if_fail(key != NULL);
	json_builder_set_member_name(builder, key);
	json_builder_add_boolean_value(builder, value);
}

/**
 * fwupd_codec_json_append_strv:
 * @builder: (not nullable): a #JsonBuilder
 * @key: (not nullable): a string
 * @value: a #GStrv
 *
 * Appends a key and string array to a JSON builder.
 *
 * Since: 2.0.0
 */
void
fwupd_codec_json_append_strv(JsonBuilder *builder, const gchar *key, gchar **value)
{
	g_return_if_fail(JSON_IS_BUILDER(builder));
	g_return_if_fail(key != NULL);
	if (value == NULL)
		return;
	json_builder_set_member_name(builder, key);
	json_builder_begin_array(builder);
	for (guint i = 0; value[i] != NULL; i++)
		json_builder_add_string_value(builder, value[i]);
	json_builder_end_array(builder);
}
