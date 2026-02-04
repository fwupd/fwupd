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
 * FwupdJsonArray:
 *
 * A JSON array.
 *
 * See also: [struct@FwupdJsonObject] [struct@FwupdJsonNode]
 */

struct FwupdJsonArray {
	grefcount refcount;
	GPtrArray *nodes; /* of FwupdJsonNode */
};

/**
 * fwupd_json_array_new: (skip):
 *
 * Creates a new JSON array.
 *
 * Returns: (transfer full): a #FwupdJsonArray
 *
 * Since: 2.1.1
 **/
FwupdJsonArray *
fwupd_json_array_new(void)
{
	FwupdJsonArray *self = g_new0(FwupdJsonArray, 1);
	g_ref_count_init(&self->refcount);
	self->nodes = g_ptr_array_new_with_free_func((GDestroyNotify)fwupd_json_node_unref);
	return self;
}

/**
 * fwupd_json_array_ref: (skip):
 * @self: a #FwupdJsonArray
 *
 * Increases the reference count of a JSON array.
 *
 * Returns: (transfer full): a #FwupdJsonArray
 *
 * Since: 2.1.1
 **/
FwupdJsonArray *
fwupd_json_array_ref(FwupdJsonArray *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_ref_count_inc(&self->refcount);
	return self;
}

/**
 * fwupd_json_array_unref: (skip):
 * @self: a #FwupdJsonArray
 *
 * Decreases the reference count of a JSON array.
 *
 * Returns: (transfer none): a #FwupdJsonArray, or %NULL
 *
 * Since: 2.1.1
 **/
FwupdJsonArray *
fwupd_json_array_unref(FwupdJsonArray *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	if (!g_ref_count_dec(&self->refcount))
		return self;
	g_ptr_array_unref(self->nodes);
	g_free(self);
	return NULL;
}

/**
 * fwupd_json_array_get_size:
 * @self: a #FwupdJsonArray
 *
 * Gets the size of the JSON array.
 *
 * Returns: number of elements added
 *
 * Since: 2.1.1
 **/
guint
fwupd_json_array_get_size(FwupdJsonArray *self)
{
	g_return_val_if_fail(self != NULL, G_MAXUINT);
	return self->nodes->len;
}

/**
 * fwupd_json_array_get_node: (skip):
 * @self: a #FwupdJsonArray
 * @idx: index into the array
 * @error: (nullable): optional return location for an error
 *
 * Gets a node from a JSON array.
 *
 * Returns: (transfer full): a #FwupdJsonObject, or %NULL for error
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_array_get_node(FwupdJsonArray *self, guint idx, GError **error)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (idx >= self->nodes->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "index %u is larger than array size",
			    idx);
		return NULL;
	}
	return fwupd_json_node_ref(g_ptr_array_index(self->nodes, idx));
}

/**
 * fwupd_json_array_get_raw: (skip):
 * @self: a #FwupdJsonArray
 * @idx: index into the array
 * @error: (nullable): optional return location for an error
 *
 * Gets a raw value from a JSON array.
 *
 * Returns: a string, or %NULL for error
 *
 * Since: 2.1.1
 **/
GRefString *
fwupd_json_array_get_raw(FwupdJsonArray *self, guint idx, GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_node = fwupd_json_array_get_node(self, idx, error);
	if (json_node == NULL)
		return NULL;
	return fwupd_json_node_get_raw(json_node, error);
}

/**
 * fwupd_json_array_get_string: (skip):
 * @self: a #FwupdJsonArray
 * @idx: index into the array
 * @error: (nullable): optional return location for an error
 *
 * Gets a string from a JSON array.
 *
 * Returns: a string, or %NULL for error
 *
 * Since: 2.1.1
 **/
GRefString *
fwupd_json_array_get_string(FwupdJsonArray *self, guint idx, GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_node = fwupd_json_array_get_node(self, idx, error);
	if (json_node == NULL)
		return NULL;
	return fwupd_json_node_get_string(json_node, error);
}

/**
 * fwupd_json_array_get_object: (skip):
 * @self: a #FwupdJsonArray
 * @idx: index into the array
 * @error: (nullable): optional return location for an error
 *
 * Gets an object from a JSON array.
 *
 * Returns: (transfer full): a #FwupdJsonObject, or %NULL for error
 *
 * Since: 2.1.1
 **/
FwupdJsonObject *
fwupd_json_array_get_object(FwupdJsonArray *self, guint idx, GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_node = fwupd_json_array_get_node(self, idx, error);
	if (json_node == NULL)
		return NULL;
	return fwupd_json_node_get_object(json_node, error);
}

/**
 * fwupd_json_array_get_array: (skip):
 * @self: a #FwupdJsonArray
 * @idx: index into the array
 * @error: (nullable): optional return location for an error
 *
 * Gets another array from a JSON array.
 *
 * Returns: (transfer full): a #FwupdJsonArray, or %NULL for error
 *
 * Since: 2.1.1
 **/
FwupdJsonArray *
fwupd_json_array_get_array(FwupdJsonArray *self, guint idx, GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_node = fwupd_json_array_get_node(self, idx, error);
	if (json_node == NULL)
		return NULL;
	return fwupd_json_node_get_array(json_node, error);
}

void
fwupd_json_array_add_string_internal(FwupdJsonArray *self, GRefString *value)
{
	g_ptr_array_add(self->nodes, fwupd_json_node_new_string_internal(value));
}

/**
 * fwupd_json_array_add_node:
 * @self: a #FwupdJsonArray
 * @json_node: (not nullable): string value
 *
 * Adds a node to a JSON array.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_add_node(FwupdJsonArray *self, FwupdJsonNode *json_node)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(json_node != NULL);
	g_ptr_array_add(self->nodes, fwupd_json_node_ref(json_node));
}

/**
 * fwupd_json_array_add_string:
 * @self: a #FwupdJsonArray
 * @value: (not nullable): string value
 *
 * Adds a string to a JSON array.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_add_string(FwupdJsonArray *self, const gchar *value)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(value != NULL);
	g_ptr_array_add(self->nodes, fwupd_json_node_new_string(value));
}

void
fwupd_json_array_add_raw_internal(FwupdJsonArray *self, GRefString *value)
{
	g_ptr_array_add(self->nodes, fwupd_json_node_new_raw_internal(value));
}

/**
 * fwupd_json_array_add_raw:
 * @self: a #FwupdJsonArray
 * @value: (not nullable): string value
 *
 * Adds a raw value to a JSON array.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_add_raw(FwupdJsonArray *self, const gchar *value)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(value != NULL);
	g_ptr_array_add(self->nodes, fwupd_json_node_new_raw(value));
}

/**
 * fwupd_json_array_add_object:
 * @self: a #FwupdJsonArray
 * @json_obj: a #FwupdJsonObject
 *
 * Adds an object to a JSON array.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_add_object(FwupdJsonArray *self, FwupdJsonObject *json_obj)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(json_obj != NULL);
	g_ptr_array_add(self->nodes, fwupd_json_node_new_object(json_obj));
}

/**
 * fwupd_json_array_add_array:
 * @self: a #FwupdJsonArray
 * @json_arr: a #FwupdJsonArray
 *
 * Adds a different array to a JSON array.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_add_array(FwupdJsonArray *self, FwupdJsonArray *json_arr)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(json_arr != NULL);
	g_return_if_fail(self != json_arr);
	g_ptr_array_add(self->nodes, fwupd_json_node_new_array(json_arr));
}

/**
 * fwupd_json_array_add_bytes:
 * @self: a #FwupdJsonArray
 * @value: (not nullable): string value
 *
 * Adds bytes to a JSON array. They will be base64 encoded as a string.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_add_bytes(FwupdJsonArray *self, GBytes *value)
{
	g_autofree gchar *b64data = NULL;
	const guint8 *buf;
	gsize bufsz = 0;

	g_return_if_fail(self != NULL);
	g_return_if_fail(value != NULL);

	buf = g_bytes_get_data(value, &bufsz);
	b64data = g_base64_encode(buf, bufsz);

	g_ptr_array_add(self->nodes, fwupd_json_node_new_string(b64data));
}

/**
 * fwupd_json_array_append_string:
 * @self: a #FwupdJsonArray
 * @str: a #GString
 * @depth: current depth, where 0 is the root json_node
 * @flags: some #FwupdJsonExportFlags e.g. #FWUPD_JSON_EXPORT_FLAG_INDENT
 *
 * Appends the JSON array to existing string.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_array_append_string(FwupdJsonArray *self,
			       GString *str,
			       guint depth,
			       FwupdJsonExportFlags flags)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(str != NULL);

	/* start */
	g_string_append_c(str, '[');
	if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT)
		g_string_append_c(str, '\n');

	for (guint i = 0; i < self->nodes->len; i++) {
		FwupdJsonNode *json_node = g_ptr_array_index(self->nodes, i);
		if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT)
			fwupd_json_indent(str, depth + 1);
		fwupd_json_node_append_string(json_node, str, depth + 1, flags);
		if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT) {
			if (i != self->nodes->len - 1)
				g_string_append_c(str, ',');
			g_string_append_c(str, '\n');
		} else {
			if (i != self->nodes->len - 1)
				g_string_append(str, ", ");
		}
	}

	/* end */
	if (flags & FWUPD_JSON_EXPORT_FLAG_INDENT)
		fwupd_json_indent(str, depth);
	g_string_append_c(str, ']');
}

/**
 * fwupd_json_array_to_string:
 * @self: a #FwupdJsonArray
 * @flags: some #FwupdJsonExportFlags e.g. #FWUPD_JSON_EXPORT_FLAG_INDENT
 *
 * Converts the JSON array to a string representation.
 *
 * Returns: (transfer full): a #GString
 *
 * Since: 2.1.1
 **/
GString *
fwupd_json_array_to_string(FwupdJsonArray *self, FwupdJsonExportFlags flags)
{
	GString *str = g_string_new(NULL);
	fwupd_json_array_append_string(self, str, 0, flags);
	return str;
}
