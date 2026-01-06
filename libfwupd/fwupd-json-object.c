/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-json-array-private.h"
#include "fwupd-json-common-private.h"
#include "fwupd-json-node-private.h"
#include "fwupd-json-object-private.h"

/**
 * FwupdJsonObject:
 *
 * A JSON object.
 *
 * See also: [struct@FwupdJsonArray] [struct@FwupdJsonNode]
 */

typedef struct {
	GRefString *key;
	FwupdJsonNode *json_node;
} FwupdJsonObjectEntry;

struct FwupdJsonObject {
	grefcount refcount;
	GPtrArray *items; /* element-type FwupdJsonObjectEntry */
};

static void
fwupd_json_object_entry_free(FwupdJsonObjectEntry *entry)
{
	g_ref_string_release(entry->key);
	fwupd_json_node_unref(entry->json_node);
	g_free(entry);
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
	FwupdJsonObject *self = g_new0(FwupdJsonObject, 1);
	g_ref_count_init(&self->refcount);
	self->items = g_ptr_array_new_with_free_func((GDestroyNotify)fwupd_json_object_entry_free);
	return self;
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
	g_ptr_array_unref(self->items);
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
	g_ptr_array_set_size(self->items, 0);
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
	return self->items->len;
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (idx >= self->items->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "index %u is larger than object size",
			    idx);
		return NULL;
	}

	/* success */
	entry = g_ptr_array_index(self->items, idx);
	return entry->key;
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (idx >= self->items->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "index %u is larger than object size",
			    idx);
		return NULL;
	}

	/* success */
	entry = g_ptr_array_index(self->items, idx);
	return fwupd_json_node_ref(entry->json_node);
}

static FwupdJsonObjectEntry *
fwupd_json_object_get_entry(FwupdJsonObject *self, const gchar *key, GError **error)
{
	for (guint i = 0; i < self->items->len; i++) {
		FwupdJsonObjectEntry *entry = g_ptr_array_index(self->items, i);
		if (g_strcmp0(key, entry->key) == 0)
			return entry;
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no json_node for key %s", key);
	return NULL;
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	entry = fwupd_json_object_get_entry(self, key, error);
	if (entry == NULL)
		return NULL;
	return fwupd_json_node_get_string(entry->json_node, error);
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(value_default != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry == NULL)
		return value_default;
	return fwupd_json_node_get_string(entry->json_node, error);
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
	FwupdJsonObjectEntry *entry;
	const gchar *str;
	gchar *endptr = NULL;
	gint64 value_tmp;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	entry = fwupd_json_object_get_entry(self, key, error);
	if (entry == NULL)
		return FALSE;
	str = fwupd_json_node_get_raw(entry->json_node, error);
	if (str == NULL)
		return FALSE;

	/* convert */
	value_tmp = g_ascii_strtoll(str, &endptr, 10); /* nocheck:blocked */
	if ((gsize)(endptr - str) != strlen(str)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "cannot parse %s", str);
		return FALSE;
	}

	/* overflow check */
	if (value_tmp == G_MAXINT64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot parse %s due to overflow",
			    str);
		return FALSE;
	}

	/* success */
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
	FwupdJsonObjectEntry *entry;
	const gchar *str;
	gchar *endptr = NULL;
	gint64 value_tmp;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry == NULL) {
		if (value != NULL)
			*value = value_default;
		return TRUE;
	}
	str = fwupd_json_node_get_raw(entry->json_node, error);
	if (str == NULL)
		return FALSE;

	/* convert */
	value_tmp = g_ascii_strtoll(str, &endptr, 10); /* nocheck:blocked */
	if ((gsize)(endptr - str) != strlen(str)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "cannot parse %s", str);
		return FALSE;
	}

	/* overflow check */
	if (value_tmp == G_MAXINT64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot parse %s due to overflow",
			    str);
		return FALSE;
	}

	/* success */
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
	FwupdJsonObjectEntry *entry;
	const gchar *str;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	entry = fwupd_json_object_get_entry(self, key, error);
	if (entry == NULL)
		return FALSE;
	str = fwupd_json_node_get_raw(entry->json_node, error);
	if (str == NULL)
		return FALSE;

	/* convert */
	if (g_ascii_strcasecmp(str, "false") == 0) {
		if (value != NULL)
			*value = FALSE;
		return TRUE;
	}
	if (g_ascii_strcasecmp(str, "true") == 0) {
		if (value != NULL)
			*value = TRUE;
		return TRUE;
	}

	/* failed */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "cannot parse %s", str);
	return FALSE;
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
	FwupdJsonObjectEntry *entry;
	const gchar *str;

	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry == NULL) {
		if (value != NULL)
			*value = value_default;
		return TRUE;
	}
	str = fwupd_json_node_get_raw(entry->json_node, error);
	if (str == NULL)
		return FALSE;

	/* convert */
	if (g_ascii_strcasecmp(str, "false") == 0) {
		if (value != NULL)
			*value = FALSE;
		return TRUE;
	}
	if (g_ascii_strcasecmp(str, "true") == 0) {
		if (value != NULL)
			*value = TRUE;
		return TRUE;
	}

	/* failed */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "cannot parse %s", str);
	return FALSE;
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
	return fwupd_json_object_get_entry(self, key, NULL) != NULL;
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	entry = fwupd_json_object_get_entry(self, key, error);
	if (entry == NULL)
		return NULL;
	return fwupd_json_node_ref(entry->json_node);
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
	g_autoptr(GPtrArray) json_nodes =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fwupd_json_node_unref);

	g_return_val_if_fail(self != NULL, NULL);

	for (guint i = 0; i < self->items->len; i++) {
		FwupdJsonObjectEntry *entry = g_ptr_array_index(self->items, i);
		g_ptr_array_add(json_nodes, fwupd_json_node_ref(entry->json_node));
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
	g_autoptr(GPtrArray) json_keys =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_ref_string_release);

	g_return_val_if_fail(self != NULL, NULL);

	for (guint i = 0; i < self->items->len; i++) {
		FwupdJsonObjectEntry *entry = g_ptr_array_index(self->items, i);
		g_ptr_array_add(json_keys, g_ref_string_acquire(entry->key));
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	entry = fwupd_json_object_get_entry(self, key, error);
	if (entry == NULL)
		return NULL;
	return fwupd_json_node_get_object(entry->json_node, error);
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
	FwupdJsonObjectEntry *entry;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	entry = fwupd_json_object_get_entry(self, key, error);
	if (entry == NULL)
		return NULL;
	return fwupd_json_node_get_array(entry->json_node, error);
}

void
fwupd_json_object_add_raw_internal(FwupdJsonObject *self,
				   GRefString *key,
				   GRefString *value,
				   FwupdJsonLoadFlags flags)
{
	FwupdJsonObjectEntry *entry = NULL;

	if ((flags & FWUPD_JSON_LOAD_FLAG_TRUSTED) == 0)
		entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry != NULL) {
		fwupd_json_node_unref(entry->json_node);
	} else {
		entry = g_new0(FwupdJsonObjectEntry, 1);
		entry->key = (flags & FWUPD_JSON_LOAD_FLAG_STATIC_KEYS) > 0
				 ? g_ref_string_new_intern(key)
				 : g_ref_string_acquire(key);
		g_ptr_array_add(self->items, entry);
	}
	entry->json_node = fwupd_json_node_new_raw_internal(value);
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
	FwupdJsonObjectEntry *entry;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(json_node != NULL);

	entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry != NULL) {
		fwupd_json_node_unref(entry->json_node);
	} else {
		entry = g_new0(FwupdJsonObjectEntry, 1);
		entry->key = g_ref_string_new(key);
		g_ptr_array_add(self->items, entry);
	}
	entry->json_node = fwupd_json_node_ref(json_node);
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
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	json_node = fwupd_json_node_new_raw(value);
	fwupd_json_object_add_node(self, key, json_node);
}

void
fwupd_json_object_add_string_internal(FwupdJsonObject *self,
				      GRefString *key,
				      GRefString *value,
				      FwupdJsonLoadFlags flags)
{
	FwupdJsonObjectEntry *entry = NULL;

	if ((flags & FWUPD_JSON_LOAD_FLAG_TRUSTED) == 0)
		entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry != NULL) {
		fwupd_json_node_unref(entry->json_node);
	} else {
		entry = g_new0(FwupdJsonObjectEntry, 1);
		entry->key = (flags & FWUPD_JSON_LOAD_FLAG_STATIC_KEYS) > 0
				 ? g_ref_string_new_intern(key)
				 : g_ref_string_acquire(key);
		g_ptr_array_add(self->items, entry);
	}
	entry->json_node = fwupd_json_node_new_string_internal(value);
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
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);

	json_node = fwupd_json_node_new_string(value);
	fwupd_json_object_add_node(self, key, json_node);
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
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	for (guint i = 0; value[i] != NULL; i++)
		fwupd_json_array_add_string(json_arr, value[i]);
	fwupd_json_object_add_array(self, key, json_arr);
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
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autofree gchar *str = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != G_MAXINT64);

	str = g_strdup_printf("%" G_GINT64_FORMAT, value);
	json_node = fwupd_json_node_new_raw(str);
	fwupd_json_object_add_node(self, key, json_node);
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
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);

	json_node = fwupd_json_node_new_raw(value ? "true" : "false");
	fwupd_json_object_add_node(self, key, json_node);
}

void
fwupd_json_object_add_object_internal(FwupdJsonObject *self,
				      GRefString *key,
				      FwupdJsonObject *json_obj)
{
	FwupdJsonObjectEntry *entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry != NULL) {
		fwupd_json_node_unref(entry->json_node);
	} else {
		entry = g_new0(FwupdJsonObjectEntry, 1);
		entry->key = g_ref_string_acquire(key);
		g_ptr_array_add(self->items, entry);
	}
	entry->json_node = fwupd_json_node_new_object(json_obj);
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
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(json_obj != NULL);
	g_return_if_fail(self != json_obj);

	json_node = fwupd_json_node_new_object(json_obj);
	fwupd_json_object_add_node(self, key, json_node);
}

/**
 * fwupd_json_object_add_object_map:
 * @self: a #FwupdJsonObject
 * @key: (not nullable): dictionary key
 * @value: (element-type utf8 utf8): a hash table
 *
 * Adds a  object to the JSON object.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_add_object_map(FwupdJsonObject *self, const gchar *key, GHashTable *value)
{
	GHashTableIter iter;
	gpointer hash_key, hash_value;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	g_hash_table_iter_init(&iter, value);
	while (g_hash_table_iter_next(&iter, &hash_key, &hash_value)) {
		fwupd_json_object_add_string(json_obj,
					     (const gchar *)hash_key,
					     (const gchar *)hash_value);
	}
	fwupd_json_object_add_object(self, key, json_obj);
}

void
fwupd_json_object_add_array_internal(FwupdJsonObject *self,
				     GRefString *key,
				     FwupdJsonArray *json_arr)
{
	FwupdJsonObjectEntry *entry = fwupd_json_object_get_entry(self, key, NULL);
	if (entry != NULL) {
		fwupd_json_node_unref(entry->json_node);
	} else {
		entry = g_new0(FwupdJsonObjectEntry, 1);
		entry->key = g_ref_string_acquire(key);
		g_ptr_array_add(self->items, entry);
	}
	entry->json_node = fwupd_json_node_new_array(json_arr);
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
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_if_fail(self != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(json_arr != NULL);

	json_node = fwupd_json_node_new_array(json_arr);
	fwupd_json_object_add_node(self, key, json_node);
}

/**
 * fwupd_json_object_append_string:
 * @self: a #FwupdJsonObject
 * @str: a #GString
 * @depth: depth, where 0 is the root node
 * @flags: some #FwupdJsonExportFlags e.g. #FWUPD_JSON_EXPORT_FLAG_INDENT
 *
 * Appends the JSON object to existing string.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_object_append_string(FwupdJsonObject *self,
				GString *str,
				guint depth,
				FwupdJsonExportFlags flags)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(str != NULL);

	/* start */
	g_string_append_c(str, '{');
	if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT)
		g_string_append_c(str, '\n');

	for (guint i = 0; i < self->items->len; i++) {
		FwupdJsonObjectEntry *entry = g_ptr_array_index(self->items, i);

		if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT)
			fwupd_json_indent(str, depth + 1);
		g_string_append_printf(str, "\"%s\": ", entry->key);
		fwupd_json_node_append_string(entry->json_node, str, depth + 1, flags);
		if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT) {
			if (i != self->items->len - 1)
				g_string_append_c(str, ',');
			g_string_append_c(str, '\n');
		} else {
			if (i != self->items->len - 1)
				g_string_append(str, ", ");
		}
	}

	/* end */
	if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT)
		fwupd_json_indent(str, depth);
	g_string_append_c(str, '}');
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
	GString *str = g_string_new(NULL);
	fwupd_json_object_append_string(self, str, 0, flags);
	if (flags & FWUPD_JSON_EXPORT_FLAG_TRAILING_NEWLINE)
		g_string_append_c(str, '\n');
	return str;
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
	GString *str = g_string_new(NULL);
	fwupd_json_object_append_string(self, str, 0, flags);
	if (flags & FWUPD_JSON_EXPORT_FLAG_TRAILING_NEWLINE)
		g_string_append_c(str, '\n');
	return g_string_free_to_bytes(str);
}
