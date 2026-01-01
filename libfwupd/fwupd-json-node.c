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
	FwupdJsonNodeKind kind;
	gpointer data;
	GDestroyNotify destroy_func;
};

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
	return self->kind;
}

static FwupdJsonNode *
fwupd_json_node_new_internal(void)
{
	FwupdJsonNode *self = g_new0(FwupdJsonNode, 1);
	g_ref_count_init(&self->refcount);
	return self;
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
	g_autoptr(FwupdJsonNode) self = fwupd_json_node_new_internal();

	g_return_val_if_fail(value != NULL, NULL);

	self->kind = FWUPD_JSON_NODE_KIND_RAW;
	self->data = g_ref_string_new(value);
	self->destroy_func = (GDestroyNotify)g_ref_string_release;
	return g_steal_pointer(&self);
}

FwupdJsonNode *
fwupd_json_node_new_raw_internal(GRefString *value)
{
	FwupdJsonNode *self = fwupd_json_node_new_internal();
	self->kind = FWUPD_JSON_NODE_KIND_RAW;
	self->data = g_ref_string_acquire(value);
	self->destroy_func = (GDestroyNotify)g_ref_string_release;
	return self;
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
	g_autoptr(FwupdJsonNode) self = fwupd_json_node_new_internal();
	self->kind = FWUPD_JSON_NODE_KIND_STRING;
	if (value != NULL) {
		self->data = g_ref_string_new(value);
		self->destroy_func = (GDestroyNotify)g_ref_string_release;
	}
	return g_steal_pointer(&self);
}

FwupdJsonNode *
fwupd_json_node_new_string_internal(GRefString *value)
{
	FwupdJsonNode *self = fwupd_json_node_new_internal();
	self->kind = FWUPD_JSON_NODE_KIND_STRING;
	if (value != NULL) {
		self->data = g_ref_string_acquire(value);
		self->destroy_func = (GDestroyNotify)g_ref_string_release;
	}
	return self;
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
	g_autoptr(FwupdJsonNode) self = fwupd_json_node_new_internal();

	g_return_val_if_fail(json_obj != NULL, NULL);

	self->kind = FWUPD_JSON_NODE_KIND_OBJECT;
	self->data = fwupd_json_object_ref(json_obj);
	self->destroy_func = (GDestroyNotify)fwupd_json_object_unref;
	return g_steal_pointer(&self);
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
	g_autoptr(FwupdJsonNode) self = fwupd_json_node_new_internal();

	g_return_val_if_fail(json_arr != NULL, NULL);

	self->kind = FWUPD_JSON_NODE_KIND_ARRAY;
	self->data = fwupd_json_array_ref(json_arr);
	self->destroy_func = (GDestroyNotify)fwupd_json_array_unref;
	return g_steal_pointer(&self);
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
	if (self->destroy_func != NULL)
		self->destroy_func(self->data);
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
	g_return_val_if_fail(self != NULL, NULL);
	if (self->kind != FWUPD_JSON_NODE_KIND_OBJECT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "json_node kind was %s, not object",
			    fwupd_json_node_kind_to_string(self->kind));
		return NULL;
	}
	return fwupd_json_object_ref((FwupdJsonObject *)self->data);
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
	g_return_val_if_fail(self != NULL, NULL);
	if (self->kind != FWUPD_JSON_NODE_KIND_ARRAY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "json_node kind was %s, not array",
			    fwupd_json_node_kind_to_string(self->kind));
		return NULL;
	}
	return fwupd_json_array_ref((FwupdJsonArray *)self->data);
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
	g_return_val_if_fail(self != NULL, NULL);
	if (self->kind != FWUPD_JSON_NODE_KIND_RAW) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "json_node kind was %s, not raw",
			    fwupd_json_node_kind_to_string(self->kind));
		return NULL;
	}
	return (GRefString *)self->data;
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
	g_return_val_if_fail(self != NULL, NULL);
	if (self->kind != FWUPD_JSON_NODE_KIND_STRING) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "json_node kind was %s, not string",
			    fwupd_json_node_kind_to_string(self->kind));
		return NULL;
	}
	if (self->data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "value was null");
		return NULL;
	}
	return (GRefString *)self->data;
}

static void
fwupd_json_node_append_string_safe(FwupdJsonNode *self, GString *str)
{
	const gchar *tmp = (const gchar *)self->data;

	/* no quotes */
	if (tmp == NULL) {
		g_string_append(str, "null");
		return;
	}

	/* quoted and escaped */
	g_string_append_c(str, '\"');
	for (guint i = 0; tmp[i] != '\0'; i++) {
		if (tmp[i] == '\\') {
			g_string_append(str, "\\\\");
		} else if (tmp[i] == '\n') {
			g_string_append(str, "\\n");
		} else if (tmp[i] == '\t') {
			g_string_append(str, "\\t");
		} else if (tmp[i] == '\"') {
			g_string_append(str, "\\\"");
		} else {
			g_string_append_c(str, tmp[i]);
		}
	}
	g_string_append_c(str, '\"');
}

void
fwupd_json_node_append_string(FwupdJsonNode *self,
			      GString *str,
			      guint depth,
			      FwupdJsonExportFlags flags)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(str != NULL);

	if (self->kind == FWUPD_JSON_NODE_KIND_RAW) {
		g_string_append(str, fwupd_json_node_get_raw(self, NULL));
		return;
	}
	if (self->kind == FWUPD_JSON_NODE_KIND_STRING) {
		fwupd_json_node_append_string_safe(self, str);
		return;
	}
	if (self->kind == FWUPD_JSON_NODE_KIND_OBJECT) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_node_get_object(self, NULL);
		fwupd_json_object_append_string(json_obj, str, depth, flags);
		return;
	}
	if (self->kind == FWUPD_JSON_NODE_KIND_ARRAY) {
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_node_get_array(self, NULL);
		fwupd_json_array_append_string(json_arr, str, depth, flags);
		return;
	}
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
	g_autoptr(GString) str = g_string_new(NULL);
	g_return_val_if_fail(self != NULL, NULL);
	fwupd_json_node_append_string(self, str, 0, flags);
	return g_steal_pointer(&str);
}
