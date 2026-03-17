/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCbor"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-cbor-item-private.h"
#include "fu-input-stream.h"

struct FuCborItem {
	grefcount refcount;
	FuCborItemKind kind;
	gpointer value;
	GDestroyNotify value_destroy;
};

typedef struct {
	FuCborItem *key;
	FuCborItem *val;
} FuCborPair;

#define FU_CBOR_ITEM_TO_ARRAY(self)  ((GPtrArray *)self->value)
#define FU_CBOR_ITEM_TO_BYTES(self)  ((GBytes *)self->value)
#define FU_CBOR_ITEM_TO_STRING(self) ((gchar *)self->value)
#define FU_CBOR_ITEM_TO_INT64(self)  (*((gint64 *)self->value))
#define FU_CBOR_ITEM_TO_BOOLEAN(self) (GPOINTER_TO_UINT(self->value))

/**
 * fu_cbor_item_get_kind: (skip):
 * @self: a #FuCborItem
 *
 * Get the CBOR item kind.
 *
 * Returns: a #FuCborItemKind, e.g. %FU_CBOR_ITEM_KIND_STRING
 *
 * Since: 2.1.2
 **/
FuCborItemKind
fu_cbor_item_get_kind(FuCborItem *self)
{
	g_return_val_if_fail(self != NULL, 0);
	return self->kind;
}

/**
 * fu_cbor_item_get_integer: (skip):
 * @self: a #FuCborItem
 * @value: (nullable) (out): value
 * @error: (nullable): optional return location for an error
 *
 * Gets an integer from a CBOR item.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.1.2
 **/
gboolean
fu_cbor_item_get_integer(FuCborItem *self, gint64 *value, GError **error)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (self->kind != FU_CBOR_ITEM_KIND_INTEGER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "item kind invalid, got %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return FALSE;
	}
	if (value != NULL)
		*value = FU_CBOR_ITEM_TO_INT64(self);
	return TRUE;
}

/**
 * fu_cbor_item_get_boolean: (skip):
 * @self: a #FuCborItem
 * @value: (nullable) (out): value
 * @error: (nullable): optional return location for an error
 *
 * Gets an integer from a CBOR item.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.1.2
 **/
gboolean
fu_cbor_item_get_boolean(FuCborItem *self, gboolean *value, GError **error)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (self->kind != FU_CBOR_ITEM_KIND_BOOLEAN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "item kind invalid, got %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return FALSE;
	}
	if (value != NULL)
		*value = FU_CBOR_ITEM_TO_BOOLEAN(self);
	return TRUE;
}

/**
 * fu_cbor_item_get_string: (skip):
 * @self: a #FuCborItem
 * @error: (nullable): optional return location for an error
 *
 * Gets a string from a CBOR item.
 *
 * Returns: (transfer full): a string, or %NULL on error
 *
 * Since: 2.1.2
 **/
gchar *
fu_cbor_item_get_string(FuCborItem *self, GError **error)
{
	const gchar *str;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (self->kind != FU_CBOR_ITEM_KIND_STRING) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "item kind invalid, got %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return NULL;
	}
	str = FU_CBOR_ITEM_TO_STRING(self);
	if (str == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "empty string");
		return NULL;
	}
	return g_strdup(str);
}

/**
 * fu_cbor_item_get_bytes: (skip):
 * @self: a #FuCborItem
 * @error: (nullable): optional return location for an error
 *
 * Gets a raw bytestring from a CBOR item.
 *
 * Returns: (transfer full): a string, or %NULL on error
 *
 * Since: 2.1.2
 **/
GBytes *
fu_cbor_item_get_bytes(FuCborItem *self, GError **error)
{
	GBytes *blob;

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (self->kind != FU_CBOR_ITEM_KIND_BYTES) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "item kind invalid, got %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return NULL;
	}
	blob = FU_CBOR_ITEM_TO_BYTES(self);
	if (blob == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "empty byte array");
		return NULL;
	}
	return g_bytes_ref(blob);
}

/**
 * fu_cbor_item_array_length: (skip):
 * @self: a #FuCborItem
 *
 * Gets the length of the CBOR array.
 *
 * Returns: length
 *
 * Since: 2.1.2
 **/
guint
fu_cbor_item_array_length(FuCborItem *self)
{
	g_return_val_if_fail(self != NULL, G_MAXUINT);
	g_return_val_if_fail(self->kind == FU_CBOR_ITEM_KIND_ARRAY, G_MAXUINT);
	return FU_CBOR_ITEM_TO_ARRAY(self)->len;
}

/**
 * fu_cbor_item_map_length: (skip):
 * @self: a #FuCborItem
 *
 * Gets the length of the CBOR map.
 *
 * Returns: length, or %G_MAXUINT for error
 *
 * Since: 2.1.2
 **/
guint
fu_cbor_item_map_length(FuCborItem *self)
{
	g_return_val_if_fail(self != NULL, G_MAXUINT);
	g_return_val_if_fail(self->kind == FU_CBOR_ITEM_KIND_MAP, G_MAXUINT);
	return FU_CBOR_ITEM_TO_ARRAY(self)->len;
}

/**
 * fu_cbor_item_array_index: (skip):
 * @self: a #FuCborItem
 * @idx: an index
 *
 * Gets a specific item in an array.
 *
 * Returns: a #FuCborItem, or %NULL for error
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_array_index(FuCborItem *self, guint idx)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(self->kind == FU_CBOR_ITEM_KIND_ARRAY, NULL);
	g_return_val_if_fail(idx < FU_CBOR_ITEM_TO_ARRAY(self)->len, NULL);
	return g_ptr_array_index(FU_CBOR_ITEM_TO_ARRAY(self), idx);
}

/**
 * fu_cbor_item_map_index: (skip):
 * @self: a #FuCborItem
 * @idx: an index
 * @item_key: (optional) (out) (transfer none): key
 * @item_value: (optional) (out) (transfer none): value
 *
 * Gets a specific pair from a map.
 *
 * Since: 2.1.2
 **/
void
fu_cbor_item_map_index(FuCborItem *self, guint idx, FuCborItem **item_key, FuCborItem **item_value)
{
	FuCborPair *pair;
	g_return_if_fail(self != NULL);
	g_return_if_fail(self->kind == FU_CBOR_ITEM_KIND_MAP);
	g_return_if_fail(idx < FU_CBOR_ITEM_TO_ARRAY(self)->len);
	pair = g_ptr_array_index(FU_CBOR_ITEM_TO_ARRAY(self), idx);
	if (item_key != NULL)
		*item_key = pair->key;
	if (item_value != NULL)
		*item_value = pair->val;
}

static void
fu_cbor_item_append_raw(GByteArray *buf, FuCborTag tag, guint8 value)
{
	fu_byte_array_append_uint8(buf, (tag << 5) | value);
}

static void
fu_cbor_item_append_extended(GByteArray *buf, FuCborTag tag, gint64 value)
{
	if (value <= FU_CBOR_LEN_SHORT_MAX) {
		fu_cbor_item_append_raw(buf, tag, value);
	} else if (value <= G_MAXUINT8) {
		fu_cbor_item_append_raw(buf, tag, FU_CBOR_LEN_EXT8);
		fu_byte_array_append_uint8(buf, value);
	} else if (value <= G_MAXUINT16) {
		fu_cbor_item_append_raw(buf, tag, FU_CBOR_LEN_EXT16);
		fu_byte_array_append_uint16(buf, value, G_BIG_ENDIAN);
	} else if (value <= G_MAXUINT32) {
		fu_cbor_item_append_raw(buf, tag, FU_CBOR_LEN_EXT32);
		fu_byte_array_append_uint32(buf, value, G_BIG_ENDIAN);
	} else {
		fu_cbor_item_append_raw(buf, tag, FU_CBOR_LEN_EXT64);
		fu_byte_array_append_uint64(buf, value, G_BIG_ENDIAN);
	}
}

static gboolean
fu_cbor_item_append(FuCborItem *self, GByteArray *buf, GError **error)
{
	if (self->kind == FU_CBOR_ITEM_KIND_INTEGER) {
		gint64 value = FU_CBOR_ITEM_TO_INT64(self);
		if (value >= 0) {
			fu_cbor_item_append_extended(buf, FU_CBOR_TAG_POS_INT, value);
		} else {
			fu_cbor_item_append_extended(buf, FU_CBOR_TAG_NEG_INT, -1 - value);
		}
		return TRUE;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_BOOLEAN) {
		gboolean value = FU_CBOR_ITEM_TO_BOOLEAN(self);
		fu_cbor_item_append_extended(buf,
					     FU_CBOR_TAG_SPECIAL,
					     value ? FU_CBOR_SPECIAL_VALUE_TRUE
						   : FU_CBOR_SPECIAL_VALUE_FALSE);
		return TRUE;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_STRING) {
		const gchar *str = FU_CBOR_ITEM_TO_STRING(self);
		gsize strsz;
		if (str == NULL) {
			fu_cbor_item_append_extended(buf,
						     FU_CBOR_TAG_SPECIAL,
						     FU_CBOR_SPECIAL_VALUE_NULL);
			return TRUE;
		}
		strsz = strlen(str);
		fu_cbor_item_append_extended(buf, FU_CBOR_TAG_STRING, strsz);
		if (strsz > 0)
			g_byte_array_append(buf, (const guint8 *)str, strsz);
		return TRUE;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_BYTES) {
		GBytes *blob = FU_CBOR_ITEM_TO_BYTES(self);
		fu_cbor_item_append_extended(buf, FU_CBOR_TAG_BYTES, g_bytes_get_size(blob));
		if (g_bytes_get_size(blob) > 0)
			fu_byte_array_append_bytes(buf, blob);
		return TRUE;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_ARRAY) {
		GPtrArray *array = FU_CBOR_ITEM_TO_ARRAY(self);
		fu_cbor_item_append_extended(buf, FU_CBOR_TAG_ARRAY, array->len);
		for (guint i = 0; i < array->len; i++) {
			FuCborItem *item_tmp = g_ptr_array_index(array, i);
			if (!fu_cbor_item_append(item_tmp, buf, error))
				return FALSE;
		}
		return TRUE;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_MAP) {
		GPtrArray *array = FU_CBOR_ITEM_TO_ARRAY(self);
		fu_cbor_item_append_extended(buf, FU_CBOR_TAG_MAP, array->len);
		for (guint i = 0; i < array->len; i++) {
			FuCborPair *pair = g_ptr_array_index(array, i);
			if (!fu_cbor_item_append(pair->key, buf, error))
				return FALSE;
			if (!fu_cbor_item_append(pair->val, buf, error))
				return FALSE;
		}
		return TRUE;
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "item kind %s write unsupported",
		    fu_cbor_item_kind_to_string(self->kind));
	return FALSE;
}

/**
 * fu_cbor_item_write: (skip):
 * @self: a #FuCborItem
 * @error: (nullable): optional return location for an error
 *
 * Writes CBOR items into a buffer.
 *
 * Returns: (transfer container): buffer, or %NULL on error
 *
 * Since: 2.1.2
 **/
GByteArray *
fu_cbor_item_write(FuCborItem *self, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (self->kind != FU_CBOR_ITEM_KIND_MAP && self->kind != FU_CBOR_ITEM_KIND_ARRAY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "can only write map or array, not %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return NULL;
	}
	if (!fu_cbor_item_append(self, buf, error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_cbor_item_ref: (skip):
 * @self: a #FuCborItem
 *
 * Increases the reference count of a CBOR object.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_ref(FuCborItem *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	g_ref_count_inc(&self->refcount);
	return self;
}

/**
 * fu_cbor_item_unref: (skip):
 * @self: a #FuCborItem
 *
 * Decreases the reference count of a CBOR object.
 *
 * Returns: (transfer none): a #FuCborItem, or %NULL
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_unref(FuCborItem *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	if (!g_ref_count_dec(&self->refcount))
		return self;
	if (self->value_destroy != NULL)
		self->value_destroy(self->value);
	g_free(self);
	return NULL;
}

/**
 * fu_cbor_item_new_string: (skip):
 * @value: (nullable): initial value, or %NULL
 *
 * Creates a CBOR object representing a string.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_new_string(const gchar *value)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_STRING;
	self->value = g_strdup(value);
	self->value_destroy = g_free;
	return self;
}

/**
 * fu_cbor_item_new_bytes: (skip):
 * @value: (not nullable): initial value
 *
 * Creates a CBOR object representing a byte string.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_new_bytes(GBytes *value)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_BYTES;
	self->value = g_bytes_ref(value);
	self->value_destroy = (GDestroyNotify)g_bytes_unref;
	return self;
}

/* private */
FuCborItem *
fu_cbor_item_new_string_steal(gchar *value)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_STRING;
	self->value = value;
	self->value_destroy = g_free;
	return self;
}

/**
 * fu_cbor_item_new_integer: (skip):
 * @value: initial value
 *
 * Creates a CBOR object representing an integer.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_new_integer(gint64 value)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_INTEGER;
	self->value = g_new0(gint64, 1);
	self->value_destroy = g_free;
	*((gint64 *)self->value) = value;
	return self;
}

/**
 * fu_cbor_item_new_boolean: (skip):
 * @value: initial value
 *
 * Creates a CBOR object representing a boolean.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_new_boolean(gboolean value)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_BOOLEAN;
	self->value = GUINT_TO_POINTER(value);
	return self;
}

static void
fu_cbor_item_map_pair_free(FuCborPair *pair)
{
	fu_cbor_item_unref(pair->key);
	fu_cbor_item_unref(pair->val);
	g_free(pair);
}

/**
 * fu_cbor_item_new_map: (skip):
 *
 * Creates a CBOR object representing a map, usually a dictionary.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_new_map(void)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_MAP;
	self->value = g_ptr_array_new_with_free_func((GDestroyNotify)fu_cbor_item_map_pair_free);
	self->value_destroy = (GDestroyNotify)g_ptr_array_unref;
	return self;
}

/**
 * fu_cbor_item_new_array: (skip):
 *
 * Creates a CBOR object representing an array.
 *
 * Returns: (transfer full): a #FuCborItem
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_item_new_array(void)
{
	FuCborItem *self = g_new0(FuCborItem, 1);
	g_ref_count_init(&self->refcount);
	self->kind = FU_CBOR_ITEM_KIND_ARRAY;
	self->value = g_ptr_array_new_with_free_func((GDestroyNotify)fu_cbor_item_unref);
	self->value_destroy = (GDestroyNotify)g_ptr_array_unref;
	return self;
}

/**
 * fu_cbor_item_array_append: (skip):
 * @self: a #FuCborItem
 * @item: another #FuCborItem
 * @error: (nullable): optional return location for an error
 *
 * Adds an item to a CBOR object.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.1.2
 **/
gboolean
fu_cbor_item_array_append(FuCborItem *self, FuCborItem *item, GError **error)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (self->kind != FU_CBOR_ITEM_KIND_ARRAY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "item kind invalid, got %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return FALSE;
	}
	g_ptr_array_add(FU_CBOR_ITEM_TO_ARRAY(self), fu_cbor_item_ref(item));
	return TRUE;
}

/**
 * fu_cbor_item_map_append: (skip):
 * @self: a #FuCborItem
 * @item_key: another #FuCborItem
 * @item_val: another #FuCborItem
 * @error: (nullable): optional return location for an error
 *
 * Adds key-value items to a CBOR map.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.1.2
 **/
gboolean
fu_cbor_item_map_append(FuCborItem *self,
			FuCborItem *item_key,
			FuCborItem *item_val,
			GError **error)
{
	g_autofree FuCborPair *pair = g_new0(FuCborPair, 1);
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(item_key != NULL, FALSE);
	g_return_val_if_fail(item_val != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (self->kind != FU_CBOR_ITEM_KIND_MAP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "item kind invalid, got %s",
			    fu_cbor_item_kind_to_string(self->kind));
		return FALSE;
	}

	pair->key = fu_cbor_item_ref(item_key);
	pair->val = fu_cbor_item_ref(item_val);
	g_ptr_array_add(FU_CBOR_ITEM_TO_ARRAY(self), g_steal_pointer(&pair));
	return TRUE;
}

static void
fu_cbor_item_append_string(FuCborItem *self, GString *str)
{
	if (self->kind == FU_CBOR_ITEM_KIND_INTEGER) {
		g_string_append_printf(str, "%" G_GINT64_FORMAT, FU_CBOR_ITEM_TO_INT64(self));
		return;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_BOOLEAN) {
		gboolean value = FU_CBOR_ITEM_TO_BOOLEAN(self);
		g_string_append_printf(str, "%s", value ? "True" : "False");
		return;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_STRING) {
		const gchar *value = FU_CBOR_ITEM_TO_STRING(self);
		if (value == NULL) {
			g_string_append(str, "null");
			return;
		}
		g_string_append_printf(str, "\"%s\"", value);
		return;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_BYTES) {
		GBytes *blob = FU_CBOR_ITEM_TO_BYTES(self);
		g_autofree gchar *value = fu_bytes_to_string(blob);
		g_string_append_printf(str, "0x%s", value);
		return;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_ARRAY) {
		GPtrArray *array = FU_CBOR_ITEM_TO_ARRAY(self);
		g_string_append(str, "[");
		for (guint i = 0; i < array->len; i++) {
			FuCborItem *item_tmp = g_ptr_array_index(array, i);
			fu_cbor_item_append_string(item_tmp, str);
			if (i < array->len - 1)
				g_string_append(str, ", ");
		}
		g_string_append(str, "]");
		return;
	}
	if (self->kind == FU_CBOR_ITEM_KIND_MAP) {
		GPtrArray *array = FU_CBOR_ITEM_TO_ARRAY(self);
		g_string_append(str, "{");
		for (guint i = 0; i < array->len; i++) {
			FuCborPair *pair = g_ptr_array_index(array, i);
			fu_cbor_item_append_string(pair->key, str);
			g_string_append(str, ": ");
			fu_cbor_item_append_string(pair->val, str);
			if (i < array->len - 1)
				g_string_append(str, ", ");
		}
		g_string_append(str, "}");
		return;
	}
}

/**
 * fu_cbor_item_to_string: (skip):
 * @self: a #FuCborItem
 *
 * Converts a CBOR item to a debug string.
 *
 * Returns: string
 *
 * Since: 2.1.2
 **/
gchar *
fu_cbor_item_to_string(FuCborItem *self)
{
	GString *str = g_string_new(NULL);
	fu_cbor_item_append_string(self, str);
	return g_string_free(str, FALSE);
}
