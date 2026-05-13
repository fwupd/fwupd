/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-error.h"
#include "fwupd-jcat-item.h"
#include "fwupd-json-array.h"

struct _FwupdJcatItem {
	GObject parent_instance;
	gchar *id;
	GPtrArray *blobs;
	GPtrArray *alias_ids;
};

static void
fwupd_jcat_item_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdJcatItem,
		       fwupd_jcat_item,
		       G_TYPE_OBJECT,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_jcat_item_codec_iface_init));

static void
fwupd_jcat_item_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FwupdJcatItem *self = FWUPD_JCAT_ITEM(codec);

	fwupd_codec_string_append(str, idt, "ID", self->id);
	for (guint i = 0; i < self->alias_ids->len; i++) {
		const gchar *alias_id = g_ptr_array_index(self->alias_ids, i);
		fwupd_codec_string_append(str, idt, "AliasId", alias_id);
	}
	for (guint i = 0; i < self->blobs->len; i++) {
		FwupdJcatBlob *blob = g_ptr_array_index(self->blobs, i);
		fwupd_codec_add_string(FWUPD_CODEC(blob), idt, str);
	}
}

static void
fwupd_jcat_item_set_id(FwupdJcatItem *self, const gchar *id)
{
	g_return_if_fail(FWUPD_IS_JCAT_ITEM(self));
	g_return_if_fail(id != NULL);
	g_free(self->id);
	self->id = g_strdup(id);
}

static gboolean
fwupd_jcat_item_from_json(FwupdCodec *codec, FwupdJsonObject *json_obj, GError **error)
{
	FwupdJcatItem *self = FWUPD_JCAT_ITEM(codec);
	const gchar *id;

	/* get ID */
	id = fwupd_json_object_get_string(json_obj, "Id", error);
	if (id == NULL)
		return FALSE;
	fwupd_jcat_item_set_id(self, id);

	/* get blobs */
	if (fwupd_json_object_has_node(json_obj, "Blobs")) {
		g_autoptr(FwupdJsonArray) json_array = NULL;
		json_array = fwupd_json_object_get_array(json_obj, "Blobs", error);
		if (json_array == NULL)
			return FALSE;
		for (guint i = 0; i < fwupd_json_array_get_size(json_array); i++) {
			g_autoptr(FwupdJsonObject) json_item = NULL;
			g_autoptr(FwupdJcatBlob) blob = g_object_new(FWUPD_TYPE_JCAT_BLOB, NULL);

			json_item = fwupd_json_array_get_object(json_array, i, error);
			if (json_item == NULL)
				return FALSE;
			if (!fwupd_codec_from_json(FWUPD_CODEC(blob), json_item, error))
				return FALSE;
			fwupd_jcat_item_add_blob(self, blob);
		}
	}

	/* get alias_ids */
	if (fwupd_json_object_has_node(json_obj, "AliasIds")) {
		g_autoptr(FwupdJsonArray) json_array = NULL;
		json_array = fwupd_json_object_get_array(json_obj, "AliasIds", error);
		if (json_array == NULL)
			return FALSE;
		for (guint i = 0; i < fwupd_json_array_get_size(json_array); i++) {
			const gchar *alias_id;
			alias_id = fwupd_json_array_get_string(json_array, i, error);
			if (alias_id == NULL)
				return FALSE;
			fwupd_jcat_item_add_alias_id(self, alias_id);
		}
	}

	/* success */
	return TRUE;
}

static void
fwupd_jcat_item_add_json(FwupdCodec *codec, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FwupdJcatItem *self = FWUPD_JCAT_ITEM(codec);

	/* add metadata */
	fwupd_json_object_add_string(json_obj, "Id", self->id);

	/* add alias_ids */
	if (self->alias_ids->len > 0) {
		g_autoptr(FwupdJsonArray) json_array = fwupd_json_array_new();
		for (guint i = 0; i < self->alias_ids->len; i++) {
			const gchar *id_tmp = g_ptr_array_index(self->alias_ids, i);
			fwupd_json_array_add_string(json_array, id_tmp);
		}
		fwupd_json_object_add_array(json_obj, "AliasIds", json_array);
	}

	/* add items */
	if (self->blobs->len > 0) {
		g_autoptr(FwupdJsonArray) json_array = fwupd_json_array_new();
		for (guint i = 0; i < self->blobs->len; i++) {
			FwupdJcatBlob *blob = g_ptr_array_index(self->blobs, i);
			g_autoptr(FwupdJsonObject) json_blob = fwupd_json_object_new();
			fwupd_codec_to_json(FWUPD_CODEC(blob), json_blob, flags);
			fwupd_json_array_add_object(json_array, json_blob);
		}
		fwupd_json_object_add_array(json_obj, "Blobs", json_array);
	}
}

/**
 * fwupd_jcat_item_get_blobs:
 * @self: #FwupdJcatItem
 *
 * Gets all the blobs for this item.
 *
 * Returns: (transfer container) (element-type FwupdJcatBlob): blobs
 *
 * Since: 2.1.3
 **/
GPtrArray *
fwupd_jcat_item_get_blobs(FwupdJcatItem *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), NULL);
	return g_ptr_array_ref(self->blobs);
}

/**
 * fwupd_jcat_item_get_blobs_by_kind:
 * @self: #FwupdJcatItem
 * @kind: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 *
 * Gets the item blobs by a specific kind.
 *
 * Returns: (transfer container) (element-type FwupdJcatBlob): blobs
 *
 * Since: 2.1.3
 **/
GPtrArray *
fwupd_jcat_item_get_blobs_by_kind(FwupdJcatItem *self, FwupdJcatBlobKind kind)
{
	g_autoptr(GPtrArray) blobs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), NULL);
	g_return_val_if_fail(kind != FWUPD_JCAT_BLOB_KIND_UNKNOWN, NULL);

	for (guint i = 0; i < self->blobs->len; i++) {
		FwupdJcatBlob *blob = g_ptr_array_index(self->blobs, i);
		if (fwupd_jcat_blob_get_kind(blob) == kind)
			g_ptr_array_add(blobs, g_object_ref(blob));
	}
	return g_steal_pointer(&blobs);
}

/**
 * fwupd_jcat_item_get_blob_by_kind:
 * @self: #FwupdJcatItem
 * @kind: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 * @error: #GError, or %NULL
 *
 * Gets the item blobs by a specific kind.
 *
 * Returns: (transfer full): a blob, or %NULL
 *
 * Since: 2.1.3
 **/
FwupdJcatBlob *
fwupd_jcat_item_get_blob_by_kind(FwupdJcatItem *self, FwupdJcatBlobKind kind, GError **error)
{
	g_autoptr(GPtrArray) target_blobs = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), NULL);
	g_return_val_if_fail(kind != FWUPD_JCAT_BLOB_KIND_UNKNOWN, NULL);

	target_blobs = fwupd_jcat_item_get_blobs_by_kind(self, kind);
	if (target_blobs->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "no existing checksum of type %s",
			    fwupd_jcat_blob_kind_to_string(kind));
		return NULL;
	}
	if (target_blobs->len > 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "multiple checksums of type %s",
			    fwupd_jcat_blob_kind_to_string(kind));
		return NULL;
	}
	return g_object_ref(FWUPD_JCAT_BLOB(g_ptr_array_index(target_blobs, 0)));
}

/**
 * fwupd_jcat_item_add_blob:
 * @self: #FwupdJcatItem
 * @blob: (not nullable): #FwupdJcatBlob
 *
 * Adds a new blob to the item.
 *
 * Since: 2.1.3
 **/
void
fwupd_jcat_item_add_blob(FwupdJcatItem *self, FwupdJcatBlob *blob)
{
	g_return_if_fail(FWUPD_IS_JCAT_ITEM(self));
	g_return_if_fail(FWUPD_IS_JCAT_BLOB(blob));

	/* add */
	g_ptr_array_add(self->blobs, g_object_ref(blob));
}

/**
 * fwupd_jcat_item_get_id:
 * @self: #FwupdJcatItem
 *
 * Returns the item ID.
 *
 * Returns: (transfer none): string
 *
 * Since: 2.1.3
 **/
const gchar *
fwupd_jcat_item_get_id(FwupdJcatItem *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), NULL);
	return self->id;
}

/**
 * fwupd_jcat_item_get_id_safe:
 * @self: a #FwupdJcatItem
 * @error: (nullable): optional return location for an error
 *
 * Returns the item ID, if safe to use as a path.
 *
 * Returns: string
 *
 * Since: 2.1.3
 **/
const gchar *
fwupd_jcat_item_get_id_safe(FwupdJcatItem *self, GError **error)
{
	g_autofree gchar *id_basename = NULL;

	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (self->id == NULL || self->id[0] == '\0') {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "ID not set");
		return NULL;
	}

	/* verify there is no path component */
	id_basename = g_path_get_basename(self->id);
	if (g_strcmp0(self->id, id_basename) != 0 ||
	    g_strcmp0(id_basename, G_DIR_SEPARATOR_S) == 0 || g_strcmp0(id_basename, "..") == 0 ||
	    g_strcmp0(id_basename, ".") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "ID cannot contain path components");
		return NULL;
	}

	/* success */
	return self->id;
}

/**
 * fwupd_jcat_item_add_alias_id:
 * @self: #FwupdJcatItem
 * @id: (not nullable): An item ID alias, typically a file basename
 *
 * Adds an item alias ID. Alias IDs are matched when using functions such as
 * fwupd_jcat_file_get_item_by_id().
 *
 * Since: 2.1.3
 **/
void
fwupd_jcat_item_add_alias_id(FwupdJcatItem *self, const gchar *id)
{
	g_return_if_fail(FWUPD_IS_JCAT_ITEM(self));
	g_return_if_fail(id != NULL);
	for (guint i = 0; i < self->alias_ids->len; i++) {
		const gchar *id_tmp = g_ptr_array_index(self->alias_ids, i);
		if (g_strcmp0(id, id_tmp) == 0)
			return;
	}
	g_ptr_array_add(self->alias_ids, g_strdup(id));
}

/**
 * fwupd_jcat_item_remove_alias_id:
 * @self: #FwupdJcatItem
 * @id: (not nullable): An item ID alias, typically a file basename
 *
 * Removes an item alias ID.
 *
 * Since: 2.1.3
 **/
void
fwupd_jcat_item_remove_alias_id(FwupdJcatItem *self, const gchar *id)
{
	g_return_if_fail(FWUPD_IS_JCAT_ITEM(self));
	g_return_if_fail(id != NULL);
	for (guint i = 0; i < self->alias_ids->len; i++) {
		const gchar *id_tmp = g_ptr_array_index(self->alias_ids, i);
		if (g_strcmp0(id, id_tmp) == 0) {
			g_ptr_array_remove(self->alias_ids, (gpointer)id_tmp);
			return;
		}
	}
}

/**
 * fwupd_jcat_item_get_alias_ids:
 * @self: #FwupdJcatItem
 *
 * Gets the list of alias IDs.
 *
 * Returns: (transfer container) (element-type utf8): array
 *
 * Since: 2.1.3
 **/
GPtrArray *
fwupd_jcat_item_get_alias_ids(FwupdJcatItem *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), NULL);
	return g_ptr_array_ref(self->alias_ids);
}

/**
 * fwupd_jcat_item_has_target:
 * @self: #FwupdJcatItem
 *
 * Finds out if any of the blobs are targeting an internal checksum.
 * If this returns with success then the caller might be able to use functions like
 * fwupd_jcat_context_verify_target() supplying some target checksums.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.1.3
 **/
gboolean
fwupd_jcat_item_has_target(FwupdJcatItem *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(self), FALSE);
	for (guint i = 0; i < self->blobs->len; i++) {
		FwupdJcatBlob *blob_tmp = g_ptr_array_index(self->blobs, i);
		if (fwupd_jcat_blob_get_target(blob_tmp) != FWUPD_JCAT_BLOB_KIND_UNKNOWN)
			return TRUE;
	}
	return FALSE;
}

static void
fwupd_jcat_item_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fwupd_jcat_item_add_string;
	iface->add_json = fwupd_jcat_item_add_json;
	iface->from_json = fwupd_jcat_item_from_json;
}

static void
fwupd_jcat_item_finalize(GObject *obj)
{
	FwupdJcatItem *self = FWUPD_JCAT_ITEM(obj);
	g_free(self->id);
	if (self->blobs != NULL)
		g_ptr_array_unref(self->blobs);
	if (self->alias_ids != NULL)
		g_ptr_array_unref(self->alias_ids);
	G_OBJECT_CLASS(fwupd_jcat_item_parent_class)->finalize(obj);
}

static void
fwupd_jcat_item_class_init(FwupdJcatItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_jcat_item_finalize;
}

static void
fwupd_jcat_item_init(FwupdJcatItem *self)
{
	self->blobs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->alias_ids = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_jcat_item_new:
 * @id: (not nullable): An item ID, typically a file basename
 *
 * Creates a new item.
 *
 * Returns: a #FwupdJcatItem
 *
 * Since: 2.1.3
 **/
FwupdJcatItem *
fwupd_jcat_item_new(const gchar *id)
{
	g_autoptr(FwupdJcatItem) self = g_object_new(FWUPD_TYPE_JCAT_ITEM, NULL);
	g_return_val_if_fail(id != NULL, NULL);
	self->id = g_strdup(id);
	return g_steal_pointer(&self);
}
