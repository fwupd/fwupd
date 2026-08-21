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
 * FwupdJsonNode:
 *
 * A JSON node.
 *
 * Nodes are lazy-parsed, and can either be an "object", an "array", a "string" or "raw" -- the
 * latter which can be parsed as a float, integer, or boolean.
 *
 * See also: [struct@FwupdJsonArray] [struct@FwupdJsonObject]
 */

struct FwupdJsonNode {
	grefcount refcount;
	FwupdRsJsonNode *rs;
};

/**
 * fwupd_json_node_new_from_rust:
 * @rs: (not nullable): a Rust-side node handle (ownership transferred to the new node)
 *
 * Creates a new #FwupdJsonNode wrapping a Rust handle.
 *
 * Returns: (transfer full): a #FwupdJsonNode
 **/
FwupdJsonNode *
fwupd_json_node_new_from_rust(FwupdRsJsonNode *rs)
{
	FwupdJsonNode *self = g_new0(FwupdJsonNode, 1);
	g_ref_count_init(&self->refcount);
	self->rs = rs;
	return self;
}

/**
 * fwupd_json_node_get_rust:
 * @self: a #FwupdJsonNode
 *
 * Gets the Rust-side handle.
 *
 * Returns: (transfer none): a #FwupdRsJsonNode
 **/
FwupdRsJsonNode *
fwupd_json_node_get_rust(FwupdJsonNode *self)
{
	return self->rs;
}

/**
 * fwupd_json_node_get_kind:
 * @self: a #FwupdJsonNode
 *
 * Gets the kind of the JSON json_node.
 *
 * Returns: a #FwupdJsonNodeKind
 *
 * Since: 2.1.1
 **/
FwupdJsonNodeKind
fwupd_json_node_get_kind(FwupdJsonNode *self)
{
	g_return_val_if_fail(self != NULL, 0);
	return (FwupdJsonNodeKind)fwupd_rs_json_node_get_kind(self->rs);
}

/**
 * fwupd_json_node_new_raw: (skip):
 * @value: (not nullable): string value
 *
 * Creates a new JSON string json_node.
 *
 * Returns: (transfer full): a #FwupdJsonNode
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_node_new_raw(const gchar *value)
{
	g_return_val_if_fail(value != NULL, NULL);
	return fwupd_json_node_new_from_rust(fwupd_rs_json_node_new_raw(value));
}

/**
 * fwupd_json_node_new_string: (skip):
 * @value: (not nullable): string value
 *
 * Creates a new JSON string json_node.
 *
 * Returns: (transfer full): a #FwupdJsonNode
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_node_new_string(const gchar *value)
{
	return fwupd_json_node_new_from_rust(fwupd_rs_json_node_new_string(value));
}

/**
 * fwupd_json_node_new_object: (skip):
 * @json_obj: a #FwupdJsonObject
 *
 * Creates a new JSON object json_node.
 *
 * Returns: (transfer full): a #FwupdJsonNode
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_node_new_object(FwupdJsonObject *json_obj)
{
	g_return_val_if_fail(json_obj != NULL, NULL);
	return fwupd_json_node_new_from_rust(
	    fwupd_rs_json_node_new_object(fwupd_json_object_get_rust(json_obj)));
}

/**
 * fwupd_json_node_new_array: (skip):
 * @json_arr: a #FwupdJsonArray
 *
 * Creates a new JSON object json_node.
 *
 * Returns: (transfer full): a #FwupdJsonNode
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_node_new_array(FwupdJsonArray *json_arr)
{
	g_return_val_if_fail(json_arr != NULL, NULL);
	return fwupd_json_node_new_from_rust(
	    fwupd_rs_json_node_new_array(fwupd_json_array_get_rust(json_arr)));
}

/**
 * fwupd_json_node_ref: (skip):
 * @self: a #FwupdJsonNode
 *
 * Increases the reference count of a JSON json_node.
 *
 * Returns: (transfer full): a #FwupdJsonNode
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_node_ref(FwupdJsonNode *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_ref_count_inc(&self->refcount);
	return self;
}

/**
 * fwupd_json_node_unref:
 * @self: a #FwupdJsonNode
 *
 * Destroys a JSON json_node.
 *
 * Returns: (transfer none): a #FwupdJsonArray, or %NULL
 *
 * Since: 2.1.1
 **/
FwupdJsonNode *
fwupd_json_node_unref(FwupdJsonNode *self)
{
	g_return_val_if_fail(self != NULL, NULL);

	if (!g_ref_count_dec(&self->refcount))
		return self;
	fwupd_rs_json_node_free(self->rs);
	g_free(self);
	return NULL;
}

/**
 * fwupd_json_node_get_object: (skip):
 * @self: a #FwupdJsonNode
 * @error: (nullable): optional return location for an error
 *
 * Gets the JSON object from a JSON node.
 *
 * Returns: (transfer none): a #FwupdJsonObject, or %NULL if the node was the wrong kind.
 *
 * Since: 2.1.1
 **/
FwupdJsonObject *
fwupd_json_node_get_object(FwupdJsonNode *self, GError **error)
{
	FwupdRsJsonObject *rs;

	g_return_val_if_fail(self != NULL, NULL);

	rs = fwupd_rs_json_node_get_object(self->rs, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_object_new_from_rust(rs);
}

/**
 * fwupd_json_node_get_array: (skip):
 * @self: a #FwupdJsonNode
 * @error: (nullable): optional return location for an error
 *
 * Gets the JSON array from a JSON node.
 *
 * Returns: (transfer none): a #FwupdJsonArray, or %NULL if the node was the wrong kind.
 *
 * Since: 2.1.1
 **/
FwupdJsonArray *
fwupd_json_node_get_array(FwupdJsonNode *self, GError **error)
{
	FwupdRsJsonArray *rs;

	g_return_val_if_fail(self != NULL, NULL);

	rs = fwupd_rs_json_node_get_array(self->rs, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_array_new_from_rust(rs);
}

/**
 * fwupd_json_node_get_raw: (skip):
 * @self: a #FwupdJsonNode
 * @error: (nullable): optional return location for an error
 *
 * Gets the raw value string from a JSON node.
 *
 * Returns: a #GRefString, or %NULL if the node was the wrong kind.
 *
 * Since: 2.1.1
 **/
GRefString *
fwupd_json_node_get_raw(FwupdJsonNode *self, GError **error)
{
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);

	str = fwupd_rs_json_node_get_raw(self->rs, error);
	if (str == NULL)
		return NULL;
	return g_ref_string_new(str);
}

/**
 * fwupd_json_node_get_string: (skip):
 * @self: a #FwupdJsonNode
 * @error: (nullable): optional return location for an error
 *
 * Gets the JSON string from a JSON node.
 *
 * Returns: a #GRefString, or %NULL if the node was the wrong kind.
 *
 * Since: 2.1.1
 **/
GRefString *
fwupd_json_node_get_string(FwupdJsonNode *self, GError **error)
{
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);

	str = fwupd_rs_json_node_get_string(self->rs, error);
	if (str == NULL)
		return NULL;
	return g_ref_string_new(str);
}

/**
 * fwupd_json_node_to_string:
 * @self: a #FwupdJsonNode
 * @flags: some #FwupdJsonExportFlags e.g. #FWUPD_JSON_EXPORT_FLAG_INDENT
 *
 * Converts the JSON json_node to a string representation.
 *
 * Returns: (transfer full): a #GString
 *
 * Since: 2.1.1
 **/
GString *
fwupd_json_node_to_string(FwupdJsonNode *self, FwupdJsonExportFlags flags)
{
	g_return_val_if_fail(self != NULL, NULL);
	return fwupd_rs_json_node_to_string(self->rs, (guint)flags);
}
