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

/**
 * FwupdJsonObject:
 *
 * A JSON object.
 *
 * See also: [struct@FwupdJsonArray] [struct@FwupdJsonNode]
 */

struct FwupdJsonObject {
	grefcount refcount;
	FwupdRsJsonObject *rs;
};

/**
 * fwupd_json_object_new_from_rust:
 * @rs: (not nullable): a Rust-side object handle (ownership transferred)
 *
 * Creates a new #FwupdJsonObject wrapping a Rust handle.
 *
 * Returns: (transfer full): a #FwupdJsonObject
 **/
FwupdJsonObject *
fwupd_json_object_new_from_rust(FwupdRsJsonObject *rs)
{
	FwupdJsonObject *self = g_new0(FwupdJsonObject, 1);
	g_ref_count_init(&self->refcount);
	self->rs = rs;
	return self;
}

/**
 * fwupd_json_object_get_rust:
 * @self: a #FwupdJsonObject
 *
 * Gets the Rust-side handle.
 *
 * Returns: (transfer none): a #FwupdRsJsonObject
 **/
FwupdRsJsonObject *
fwupd_json_object_get_rust(FwupdJsonObject *self)
{
	return self->rs;
}

/**
 * fwupd_json_object_new: (skip):
 *
 * Creates a new JSON object.
 *
 * Returns: (transfer full): a #FwupdJsonObject
 *
 * Since: 2.1.1
 **/
FwupdJsonObject *
fwupd_json_object_new(void)
{
	return fwupd_json_object_new_from_rust(fwupd_rs_json_object_new());
}

/**
 * fwupd_json_object_ref: (skip):
 * @self: a #FwupdJsonObject
 *
 * Increases the reference count of a JSON object.
 *
 * Returns: (transfer full): a #FwupdJsonObject
 *
 * Since: 2.1.1
 **/
FwupdJsonObject *
fwupd_json_object_ref(FwupdJsonObject *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_ref_count_inc(&self->refcount);
	return self;
}

/**
 * fwupd_json_object_unref: (skip):
 * @self: a #FwupdJsonObject
 *
 * Decreases the reference count of a JSON object.
 *
 * Returns: (transfer none): a #FwupdJsonObject, or %NULL
 *
 * Since: 2.1.1
 **/
FwupdJsonObject *
fwupd_json_object_unref(FwupdJsonObject *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	if (!g_ref_count_dec(&self->refcount))
		return self;
	fwupd_rs_json_object_free(self->rs);
	g_free(self);
	return NULL;
}

/**
 * fwupd_json_object_clear:
 * @self: a #FwupdJsonObject
 *
 * Clears the member data for the JSON object, but does not affect the refcount.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_clear(FwupdJsonObject *self)
{
	g_return_if_fail(self != NULL);
	fwupd_rs_json_object_clear(self->rs);
}

/**
 * fwupd_json_object_get_size:
 * @self: a #FwupdJsonObject
 *
 * Gets the size of the JSON object.
 *
 * Returns: number of key-values added
 *
 * Since: 2.1.1
 **/
guint
fwupd_json_object_get_size(FwupdJsonObject *self)
{
	g_return_val_if_fail(self != NULL, G_MAXUINT);
	return fwupd_rs_json_object_get_size(self->rs);
}

/**
 * fwupd_json_object_get_key_for_index: (skip):
 * @self: a #FwupdJsonObject
 * @idx: index
 * @error: (nullable): optional return location for an error
 *
 * Gets the key for a given index position.
 *
 * Returns: a #GRefString, or %NULL on error
 *
 * Since: 2.1.1
 **/
GRefString *
fwupd_json_object_get_key_for_index(FwupdJsonObject *self, guint idx, GError **error)
{
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	str = fwupd_rs_json_object_get_key_for_index(self->rs, idx, error);
	if (str == NULL)
		return NULL;
	return g_ref_string_new(str);
}

/**
 * fwupd_json_object_get_node_for_index: (skip):
 * @self: a #FwupdJsonObject
 * @idx: index
 * @error: (nullable): optional return location for an error
 *
 * Gets the node for a given index position.
 *
 * Returns: (transfer full): a #FwupdJsonNode, or %NULL on error
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_object_get_node_for_index(FwupdJsonObject *self, guint idx, GError **error)
{
	FwupdRsJsonNode *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_object_get_node_for_index(self->rs, idx, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
}

/**
 * fwupd_json_object_get_string: (skip):
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @error: (nullable): optional return location for an error
 *
 * Gets a string from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: a #GRefString, or %NULL on error
 *
 * Since: 2.1.1
 **/
GRefString *
fwupd_json_object_get_string(FwupdJsonObject *self, const gchar *key, GError **error)
{
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	str = fwupd_rs_json_object_get_string(self->rs, key, error);
	if (str == NULL)
		return NULL;
	return g_ref_string_new(str);
}

/**
 * fwupd_json_object_get_string_with_default:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value_default: (not nullable): value to return if @key is not found
 * @error: (nullable): optional return location for an error
 *
 * Gets a string from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: a string, or %NULL on error
 *
 * Since: 2.1.1
 **/
const gchar *
fwupd_json_object_get_string_with_default(FwupdJsonObject *self,
					  const gchar *key,
					  const gchar *value_default,
					  GError **error)
{
	g_autoptr(GError) error_local = NULL;
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(value_default != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	str = fwupd_rs_json_object_get_string(self->rs, key, &error_local);
	if (str == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
			return value_default;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return NULL;
	}
	return str;
}

/**
 * fwupd_json_object_get_integer:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (out) (nullable): integer value
 * @error: (nullable): optional return location for an error
 *
 * Gets an integer from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: %TRUE if @value was parsed as an integer
 *
 * Since: 2.1.1
 **/
gboolean
fwupd_json_object_get_integer(FwupdJsonObject *self,
			      const gchar *key,
			      gint64 *value,
			      GError **error)
{
	gint64 value_tmp;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fwupd_rs_json_object_get_integer(self->rs, key, &value_tmp, error))
		return FALSE;

	if (value != NULL)
		*value = value_tmp;
	return TRUE;
}

/**
 * fwupd_json_object_get_integer_with_default:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (out) (nullable): integer value
 * @value_default: value to return if @key is not found, typically 0 or %G_MAXINT64
 * @error: (nullable): optional return location for an error
 *
 * Gets an integer from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: %TRUE if @value was parsed as an integer
 *
 * Since: 2.1.1
 **/
gboolean
fwupd_json_object_get_integer_with_default(FwupdJsonObject *self,
					   const gchar *key,
					   gint64 *value,
					   gint64 value_default,
					   GError **error)
{
	g_autoptr(GError) error_local = NULL;
	gint64 value_tmp;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fwupd_rs_json_object_get_integer(self->rs, key, &value_tmp, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			if (value != NULL)
				*value = value_default;
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	if (value != NULL)
		*value = value_tmp;
	return TRUE;
}

/**
 * fwupd_json_object_get_boolean:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (out) (nullable): boolean value
 * @error: (nullable): optional return location for an error
 *
 * Gets a boolean from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: %TRUE if @value was parsed as an integer
 *
 * Since: 2.1.1
 **/
gboolean
fwupd_json_object_get_boolean(FwupdJsonObject *self,
			      const gchar *key,
			      gboolean *value,
			      GError **error)
{
	gint value_tmp;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fwupd_rs_json_object_get_boolean(self->rs, key, &value_tmp, error))
		return FALSE;

	if (value != NULL)
		*value = (gboolean)value_tmp;
	return TRUE;
}

/**
 * fwupd_json_object_get_boolean_with_default:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (out) (nullable): boolean value
 * @value_default: value to return if @key is not found, typically %FALSE
 * @error: (nullable): optional return location for an error
 *
 * Gets a boolean from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: %TRUE if @value was parsed as an integer
 *
 * Since: 2.1.1
 **/
gboolean
fwupd_json_object_get_boolean_with_default(FwupdJsonObject *self,
					   const gchar *key,
					   gboolean *value,
					   gboolean value_default,
					   GError **error)
{
	g_autoptr(GError) error_local = NULL;
	gint value_tmp;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fwupd_rs_json_object_get_boolean(self->rs, key, &value_tmp, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			if (value != NULL)
				*value = value_default;
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	if (value != NULL)
		*value = (gboolean)value_tmp;
	return TRUE;
}

/**
 * fwupd_json_object_has_node: (skip):
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 *
 * Finds if a node exists in a JSON object.
 *
 * In general, it's nearly always better to call the type-specific method directly e.g.
 * fwupd_json_object_get_string() and handle the error.
 *
 * Returns: %TRUE if a node exists with the key.
 *
 * Since: 2.1.1
 **/
gboolean
fwupd_json_object_has_node(FwupdJsonObject *self, const gchar *key)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	return fwupd_rs_json_object_has_node(self->rs, key) != 0;
}

/**
 * fwupd_json_object_get_node: (skip):
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @error: (nullable): optional return location for an error
 *
 * Gets a node from a JSON object.
 *
 * Returns: (transfer full): a #FwupdJsonObject, or %NULL on error
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_object_get_node(FwupdJsonObject *self, const gchar *key, GError **error)
{
	FwupdRsJsonNode *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_object_get_node(self->rs, key, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
}

/**
 * fwupd_json_object_get_nodes: (skip):
 * @self: a #FwupdJsonObject
 *
 * Gets all the nodes from a JSON object.
 *
 * Returns: (transfer container) (element-type FwupdJsonNode): an array of nodes
 *
 * Since: 2.1.1
 **/
GPtrArray *
fwupd_json_object_get_nodes(FwupdJsonObject *self)
{
	guint size;
	g_autoptr(GPtrArray) json_nodes =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fwupd_json_node_unref);

	g_return_val_if_fail(self != NULL, NULL);

	size = fwupd_rs_json_object_get_size(self->rs);
	for (guint i = 0; i < size; i++) {
		FwupdRsJsonNode *rs = fwupd_rs_json_object_get_node_for_index(self->rs, i, NULL);
		if (rs != NULL)
			g_ptr_array_add(json_nodes, fwupd_json_node_new_from_rust(rs));
	}
	return g_steal_pointer(&json_nodes);
}

/**
 * fwupd_json_object_get_keys: (skip):
 * @self: a #FwupdJsonObject
 *
 * Gets all the keys from a JSON object.
 *
 * Returns: (transfer container) (element-type GRefString): an array of keys
 *
 * Since: 2.1.1
 **/
GPtrArray *
fwupd_json_object_get_keys(FwupdJsonObject *self)
{
	guint size;
	g_autoptr(GPtrArray) json_keys =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_ref_string_release);

	g_return_val_if_fail(self != NULL, NULL);

	size = fwupd_rs_json_object_get_size(self->rs);
	for (guint i = 0; i < size; i++) {
		const gchar *key = fwupd_rs_json_object_get_key_for_index(self->rs, i, NULL);
		if (key != NULL)
			g_ptr_array_add(json_keys, g_ref_string_new(key));
	}
	return g_steal_pointer(&json_keys);
}

/**
 * fwupd_json_object_get_object: (skip):
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @error: (nullable): optional return location for an error
 *
 * Gets a different object from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: (transfer full): a #FwupdJsonObject, or %NULL on error
 *
 * Since: 2.1.1
 **/
FwupdJsonObject *
fwupd_json_object_get_object(FwupdJsonObject *self, const gchar *key, GError **error)
{
	FwupdRsJsonObject *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_object_get_object(self->rs, key, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_object_new_from_rust(rs);
}

/**
 * fwupd_json_object_get_array: (skip):
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @error: (nullable): optional return location for an error
 *
 * Gets an array from a JSON object. An error is returned if @key is not the correct type.
 *
 * Returns: (transfer full): a #FwupdJsonArray, or %NULL on error
 *
 * Since: 2.1.1
 **/
FwupdJsonArray *
fwupd_json_object_get_array(FwupdJsonObject *self, const gchar *key, GError **error)
{
	FwupdRsJsonArray *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_object_get_array(self->rs, key, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_array_new_from_rust(rs);
}

/**
 * fwupd_json_object_add_node:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @json_node: (not nullable): a #FwupdJsonNode
 *
 * Adds a node to the JSON object. If the node already exists the old one is replaced.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_node(FwupdJsonObject *self, const gchar *key, FwupdJsonNode *json_node)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(json_node != NULL);
	fwupd_rs_json_object_add_node(self->rs, key, fwupd_json_node_get_rust(json_node));
}

/**
 * fwupd_json_object_add_raw:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (not nullable): value
 *
 * Adds a raw value to the JSON object. If the node already exists the old one is replaced.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_raw(FwupdJsonObject *self, const gchar *key, const gchar *value)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	fwupd_rs_json_object_add_raw(self->rs, key, value);
}

/**
 * fwupd_json_object_add_string:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (nullable): value, or %NULL
 *
 * Adds a string value to the JSON object. If the node already exists the old one is replaced.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_string(FwupdJsonObject *self, const gchar *key, const gchar *value)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	fwupd_rs_json_object_add_string(self->rs, key, value);
}

/**
 * fwupd_json_object_add_bytes:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (not nullable): value
 *
 * Adds bytes to the JSON object. They will be base64 encoded as a string.
 * If the node already exists the old one is replaced.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_bytes(FwupdJsonObject *self, const gchar *key, GBytes *value)
{
	const guint8 *buf;
	gsize bufsz = 0;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	buf = g_bytes_get_data(value, &bufsz);
	if (buf == NULL) {
		fwupd_rs_json_object_add_string(self->rs, key, "");
	} else {
		g_autofree gchar *b64data = NULL;
		/* nocheck:blocked */
		b64data = g_base64_encode(buf, bufsz);
		fwupd_rs_json_object_add_string(self->rs, key, b64data);
	}
}

/**
 * fwupd_json_object_add_array_strv:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (not nullable): value
 *
 * Adds a string array to the JSON object. If the node already exists the old one is replaced.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_array_strv(FwupdJsonObject *self, const gchar *key, gchar **value)
{
	FwupdRsJsonArray *arr_rs;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	arr_rs = fwupd_rs_json_array_new();
	for (guint i = 0; value[i] != NULL; i++)
		fwupd_rs_json_array_add_string(arr_rs, value[i]);
	fwupd_rs_json_object_add_array(self->rs, key, arr_rs);
	fwupd_rs_json_array_free(arr_rs);
}

/**
 * fwupd_json_object_add_integer:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: integer
 *
 * Adds an integer value to the JSON object.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_integer(FwupdJsonObject *self, const gchar *key, gint64 value)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != G_MAXINT64);
	fwupd_rs_json_object_add_integer(self->rs, key, value);
}

/**
 * fwupd_json_object_add_boolean:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: boolean
 *
 * Adds a boolean value to the JSON object.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_boolean(FwupdJsonObject *self, const gchar *key, gboolean value)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	fwupd_rs_json_object_add_boolean(self->rs, key, value ? 1 : 0);
}

/**
 * fwupd_json_object_add_object:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @json_obj: a #FwupdJsonObject
 *
 * Adds a different object to the JSON object.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_object(FwupdJsonObject *self, const gchar *key, FwupdJsonObject *json_obj)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(json_obj != NULL);
	g_return_if_fail(self != json_obj);
	fwupd_rs_json_object_add_object(self->rs, key, fwupd_json_object_get_rust(json_obj));
}

/**
 * fwupd_json_object_add_object_map:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (element-type utf8 utf8): a hash table
 *
 * Adds a string:string object to the JSON object.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_object_map(FwupdJsonObject *self, const gchar *key, GHashTable *value)
{
	FwupdRsJsonObject *obj_rs;
	g_autoptr(GList) keys = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	obj_rs = fwupd_rs_json_object_new();
	keys = g_list_sort(g_hash_table_get_keys(value), (GCompareFunc)g_strcmp0);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *dict_key = l->data;
		const gchar *dict_value = g_hash_table_lookup(value, dict_key);
		fwupd_rs_json_object_add_string(obj_rs, dict_key, dict_value);
	}
	fwupd_rs_json_object_add_object(self->rs, key, obj_rs);
	fwupd_rs_json_object_free(obj_rs);
}

/**
 * fwupd_json_object_add_array:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @json_arr: a #FwupdJsonArray
 *
 * Adds an array to the JSON object.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_array(FwupdJsonObject *self, const gchar *key, FwupdJsonArray *json_arr)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(json_arr != NULL);
	fwupd_rs_json_object_add_array(self->rs, key, fwupd_json_array_get_rust(json_arr));
}

/**
 * fwupd_json_object_to_string:
 * @self: a #FwupdJsonObject
 * @flags: some #FwupdJsonExportFlags e.g. #FWUPD_JSON_EXPORT_FLAG_INDENT
 *
 * Converts the JSON object to a string representation.
 *
 * Returns: (transfer full): a #GString
 *
 * Since: 2.1.1
 **/
GString *
fwupd_json_object_to_string(FwupdJsonObject *self, FwupdJsonExportFlags flags)
{
	g_return_val_if_fail(self != NULL, NULL);
	return fwupd_rs_json_object_to_string(self->rs, (guint)flags);
}

/**
 * fwupd_json_object_to_bytes:
 * @self: a #FwupdJsonObject
 * @flags: some #FwupdJsonExportFlags e.g. #FWUPD_JSON_EXPORT_FLAG_INDENT
 *
 * Converts the JSON object to UTF-8 bytes.
 *
 * Returns: (transfer full): a #GBytes
 *
 * Since: 2.1.1
 **/
GBytes *
fwupd_json_object_to_bytes(FwupdJsonObject *self, FwupdJsonExportFlags flags)
{
	GString *str;

	g_return_val_if_fail(self != NULL, NULL);

	str = fwupd_rs_json_object_to_string(self->rs, (guint)flags);
	return g_string_free_to_bytes(str);
}
