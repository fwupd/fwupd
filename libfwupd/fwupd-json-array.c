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
 * FwupdJsonArray:
 *
 * A JSON array.
 *
 * See also: [struct@FwupdJsonObject] [struct@FwupdJsonNode]
 */

struct FwupdJsonArray {
	grefcount refcount;
	FwupdRsJsonArray *rs;
};

/**
 * fwupd_json_array_new_from_rust:
 * @rs: (not nullable): a Rust-side array handle (ownership transferred)
 *
 * Creates a new #FwupdJsonArray wrapping a Rust handle.
 *
 * Returns: (transfer full): a #FwupdJsonArray
 **/
FwupdJsonArray *
fwupd_json_array_new_from_rust(FwupdRsJsonArray *rs)
{
	FwupdJsonArray *self = g_new0(FwupdJsonArray, 1);
	g_ref_count_init(&self->refcount);
	self->rs = rs;
	return self;
}

/**
 * fwupd_json_array_get_rust:
 * @self: a #FwupdJsonArray
 *
 * Gets the Rust-side handle.
 *
 * Returns: (transfer none): a #FwupdRsJsonArray
 **/
FwupdRsJsonArray *
fwupd_json_array_get_rust(FwupdJsonArray *self)
{
	return self->rs;
}

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
	return fwupd_json_array_new_from_rust(fwupd_rs_json_array_new());
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
	fwupd_rs_json_array_free(self->rs);
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
	return fwupd_rs_json_array_get_size(self->rs);
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
	FwupdRsJsonNode *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_array_get_node(self->rs, idx, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_node_new_from_rust(rs);
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
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	str = fwupd_rs_json_array_get_raw(self->rs, idx, error);
	if (str == NULL)
		return NULL;
	return g_ref_string_new(str);
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
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	str = fwupd_rs_json_array_get_string(self->rs, idx, error);
	if (str == NULL)
		return NULL;
	return g_ref_string_new(str);
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
	FwupdRsJsonObject *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_array_get_object(self->rs, idx, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_object_new_from_rust(rs);
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
	FwupdRsJsonArray *rs;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	rs = fwupd_rs_json_array_get_array(self->rs, idx, error);
	if (rs == NULL)
		return NULL;
	return fwupd_json_array_new_from_rust(rs);
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
	fwupd_rs_json_array_add_node(self->rs, fwupd_json_node_get_rust(json_node));
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
	fwupd_rs_json_array_add_string(self->rs, value);
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
	fwupd_rs_json_array_add_raw(self->rs, value);
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
	fwupd_rs_json_array_add_object(self->rs, fwupd_json_object_get_rust(json_obj));
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
	fwupd_rs_json_array_add_array(self->rs, fwupd_json_array_get_rust(json_arr));
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
	const guint8 *buf;
	gsize bufsz = 0;

	g_return_if_fail(self != NULL);
	g_return_if_fail(value != NULL);

	buf = g_bytes_get_data(value, &bufsz);
	if (buf == NULL) {
		fwupd_rs_json_array_add_string(self->rs, "");
	} else {
		g_autofree gchar *b64data = NULL;
		/* nocheck:blocked */
		b64data = g_base64_encode(buf, bufsz);
		fwupd_rs_json_array_add_string(self->rs, b64data);
	}
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
	g_return_val_if_fail(self != NULL, NULL);
	return fwupd_rs_json_array_to_string(self->rs, (guint)flags);
}
