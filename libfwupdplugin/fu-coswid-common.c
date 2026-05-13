/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-cbor-item.h"
#include "fu-coswid-common.h"

/**
 * fu_coswid_read_string:
 * @item: a #FuCborItem
 * @error: (nullable): optional return location for an error
 *
 * Reads a string value. If a bytestring is provided it is converted to a GUID.
 *
 * Returns: (transfer full): text, or %NULL on error
 *
 * Since: 1.9.17
 **/
gchar *
fu_coswid_read_string(FuCborItem *item, GError **error)
{
	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (fu_cbor_item_get_kind(item) == FU_CBOR_ITEM_KIND_STRING)
		return fu_cbor_item_get_string(item, error);
	if (fu_cbor_item_get_kind(item) == FU_CBOR_ITEM_KIND_BYTES) {
		g_autoptr(GBytes) blob = fu_cbor_item_get_bytes(item, error);
		if (blob == NULL)
			return NULL;
		if (g_bytes_get_size(blob) == 16) {
			return fwupd_guid_to_string(
			    (const fwupd_guid_t *)g_bytes_get_data(blob, NULL),
			    FWUPD_GUID_FLAG_NONE);
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "item is not a string or GUID bytestring");
	return NULL;
}

/**
 * fu_coswid_read_tag:
 * @item: a #FuCborItem
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_read_tag(FuCborItem *item, FuCoswidTag *value, GError **error)
{
	gint64 tmp = 0;

	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_cbor_item_get_integer(item, &tmp, error)) {
		g_prefix_error_literal(error, "tag invalid: ");
		return FALSE;
	}
	*value = (FuCoswidTag)tmp;
	return TRUE;
}

/**
 * fu_coswid_read_version_scheme:
 * @item: a #FuCborItem
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a version-scheme value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_read_version_scheme(FuCborItem *item, FuCoswidVersionScheme *value, GError **error)
{
	gint64 tmp = 0;

	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_cbor_item_get_integer(item, &tmp, error)) {
		g_prefix_error_literal(error, "version-scheme invalid: ");
		return FALSE;
	}
	*value = (FuCoswidVersionScheme)tmp;
	return TRUE;
}

/**
 * fu_coswid_write_tag_string:
 * @item: a #FuCborItem
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: string
 *
 * Writes a string tag value.
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_string(FuCborItem *item, FuCoswidTag tag, const gchar *value)
{
	g_autoptr(FuCborItem) key = fu_cbor_item_new_integer(tag);
	g_autoptr(FuCborItem) val = fu_cbor_item_new_string(value);
	if (!fu_cbor_item_map_append(item, key, val, NULL))
		g_critical("failed to push string to map");
}

/**
 * fu_coswid_write_tag_bytestring:
 * @item: a #FuCborItem
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @buf: a buffer of data
 * @bufsz: sizeof @buf
 *
 * Writes a bytestring tag value.
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_bytestring(FuCborItem *item, FuCoswidTag tag, const guint8 *buf, gsize bufsz)
{
	g_autoptr(GBytes) blob = g_bytes_new(buf, bufsz);
	g_autoptr(FuCborItem) key = fu_cbor_item_new_integer(tag);
	g_autoptr(FuCborItem) val = fu_cbor_item_new_bytes(blob);
	if (!fu_cbor_item_map_append(item, key, val, NULL))
		g_critical("failed to push bytestring to map");
}

/**
 * fu_coswid_write_tag_bool:
 * @item: a #FuCborItem
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: boolean
 *
 * Writes a #gboolean tag value.
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_bool(FuCborItem *item, FuCoswidTag tag, gboolean value)
{
	g_autoptr(FuCborItem) key = fu_cbor_item_new_integer(tag);
	g_autoptr(FuCborItem) val = fu_cbor_item_new_boolean(value);
	if (!fu_cbor_item_map_append(item, key, val, NULL))
		g_critical("failed to push bool to indefinite map");
}

/**
 * fu_coswid_write_tag_integer:
 * @item: a #FuCborItem
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: integer
 *
 * Writes an integer tag value.
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_integer(FuCborItem *item, FuCoswidTag tag, gint64 value)
{
	g_autoptr(FuCborItem) key = fu_cbor_item_new_integer(tag);
	g_autoptr(FuCborItem) val = fu_cbor_item_new_integer(value);
	if (!fu_cbor_item_map_append(item, key, val, NULL))
		g_critical("failed to push integer to map");
}

/**
 * fu_coswid_write_tag_item:
 * @item: a #FuCborItem
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: a #FuCborItem
 *
 * Writes a raw tag value.
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_item(FuCborItem *item, FuCoswidTag tag, FuCborItem *value)
{
	g_autoptr(FuCborItem) key = fu_cbor_item_new_integer(tag);
	if (!fu_cbor_item_map_append(item, key, value, NULL))
		g_critical("failed to push item to indefinite map");
}

/**
 * fu_coswid_parse_one_or_many:
 * @item: a #FuCborItem
 * @func: (scope call): a function to call with each map value
 * @user_data: pointer value to pass to @func
 * @error: (nullable): optional return location for an error
 *
 * Parses an item that *may* be an array, calling @func on each map value.
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_parse_one_or_many(FuCborItem *item,
			    FuCoswidItemFunc func,
			    gpointer user_data,
			    GError **error)
{
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* one */
	if (fu_cbor_item_get_kind(item) == FU_CBOR_ITEM_KIND_MAP)
		return func(item, user_data, error);

	/* many */
	if (fu_cbor_item_get_kind(item) == FU_CBOR_ITEM_KIND_ARRAY) {
		for (guint j = 0; j < fu_cbor_item_array_length(item); j++) {
			FuCborItem *value = fu_cbor_item_array_index(item, j);
			if (fu_cbor_item_get_kind(value) != FU_CBOR_ITEM_KIND_MAP) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "not an array of a map");
				return FALSE;
			}
			if (!func(value, user_data, error))
				return FALSE;
		}
		return TRUE;
	}

	/* not sure what to do */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "neither an array or map");
	return FALSE;
}
