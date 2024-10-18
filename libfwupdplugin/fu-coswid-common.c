/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <fwupd.h>

#include "fu-coswid-common.h"

#ifdef HAVE_CBOR

/**
 * fu_coswid_read_string:
 * @item: a #cbor_item_t
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a string value. If a bytestring is provided it is converted to a GUID.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gchar *
fu_coswid_read_string(cbor_item_t *item, GError **error)
{
	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (cbor_isa_string(item)) {
		if (cbor_string_handle(item) == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "item has no string set");
			return NULL;
		}
		return g_strndup((const gchar *)cbor_string_handle(item), cbor_string_length(item));
	}
	if (cbor_isa_bytestring(item) && cbor_bytestring_length(item) == 16) {
		return fwupd_guid_to_string((const fwupd_guid_t *)cbor_bytestring_handle(item),
					    FWUPD_GUID_FLAG_NONE);
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "item is not a string or GUID bytestring");
	return NULL;
}

/**
 * fu_coswid_read_byte_array:
 * @item: a #cbor_item_t
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a bytestring value as a #GByteArray.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
GByteArray *
fu_coswid_read_byte_array(cbor_item_t *item, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!cbor_isa_bytestring(item)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "item is not a bytestring");
		return NULL;
	}
	if (cbor_bytestring_handle(item) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "item has no bytestring set");
		return NULL;
	}
	g_byte_array_append(buf, cbor_bytestring_handle(item), cbor_bytestring_length(item));
	return g_steal_pointer(&buf);
}

/**
 * fu_coswid_read_tag:
 * @item: a #cbor_item_t
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
fu_coswid_read_tag(cbor_item_t *item, FuCoswidTag *value, GError **error)
{
	guint64 tmp;

	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!cbor_isa_uint(item)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "tag item is not a uint");
		return FALSE;
	}
	tmp = cbor_get_int(item);
	if (tmp > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "0x%x is too large for tag",
			    (guint)tmp);
		return FALSE;
	}
	*value = (FuCoswidTag)tmp;
	return TRUE;
}

/**
 * fu_coswid_read_version_scheme:
 * @item: a #cbor_item_t
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
fu_coswid_read_version_scheme(cbor_item_t *item, FuCoswidVersionScheme *value, GError **error)
{
	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!cbor_isa_uint(item)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "version-scheme item is not a uint");
		return FALSE;
	}
	*value = (FuCoswidVersionScheme)cbor_get_int(item);
	return TRUE;
}

/**
 * fu_coswid_read_u8:
 * @item: a #cbor_item_t
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a #guint8 tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_read_u8(cbor_item_t *item, guint8 *value, GError **error)
{
	guint64 tmp;

	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!cbor_isa_uint(item)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "value item is not a uint");
		return FALSE;
	}
	tmp = cbor_get_int(item);
	if (tmp > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "0x%x is too large for u8",
			    (guint)tmp);
		return FALSE;
	}
	*value = (guint8)tmp;
	return TRUE;
}

/**
 * fu_coswid_read_s8:
 * @item: a #cbor_item_t
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a #gint8 value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_read_s8(cbor_item_t *item, gint8 *value, GError **error)
{
	guint64 tmp;

	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!cbor_is_int(item)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "value item is not a int");
		return FALSE;
	}
	tmp = cbor_get_int(item);
	if (tmp > 127) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "0x%x is too large for s8",
			    (guint)tmp);
		return FALSE;
	}
	*value = cbor_isa_negint(item) ? (gint8)((-1) - tmp) : (gint8)tmp;
	return TRUE;
}

/**
 * fu_coswid_read_u64:
 * @item: a #cbor_item_t
 * @value: read value
 * @error: (nullable): optional return location for an error
 *
 * Reads a #guint64 tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_read_u64(cbor_item_t *item, guint64 *value, GError **error)
{
	g_return_val_if_fail(item != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!cbor_isa_uint(item)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "value item is not a uint");
		return FALSE;
	}
	*value = cbor_get_int(item);
	return TRUE;
}

/**
 * fu_coswid_write_tag_string:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: string
 *
 * Writes a string tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_string(cbor_item_t *item, FuCoswidTag tag, const gchar *value)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_string(value);
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push string to indefinite map");
}

/**
 * fu_coswid_write_tag_bytestring:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @buf: a buffer of data
 * @bufsz: sizeof @buf
 *
 * Writes a bytestring tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_bytestring(cbor_item_t *item, FuCoswidTag tag, const guint8 *buf, gsize bufsz)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_bytestring((cbor_data)buf, bufsz);
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push bytestring to indefinite map");
}

/**
 * fu_coswid_write_tag_bool:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: boolean
 *
 * Writes a #gboolean tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_bool(cbor_item_t *item, FuCoswidTag tag, gboolean value)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_bool(value);
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push bool to indefinite map");
}

/**
 * fu_coswid_write_tag_u16:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: unsigned integer
 *
 * Writes a #guint16 tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_u16(cbor_item_t *item, FuCoswidTag tag, guint16 value)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_uint16(value);
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push u16 to indefinite map");
}

/**
 * fu_coswid_write_tag_u64:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: unsigned integer
 *
 * Writes a #guint64 tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_u64(cbor_item_t *item, FuCoswidTag tag, guint64 value)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_uint64(value);
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push u64 to indefinite map");
}

/**
 * fu_coswid_write_tag_s8:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: signed integer
 *
 * Writes a #gint8 tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_s8(cbor_item_t *item, FuCoswidTag tag, gint8 value)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_new_int8();
	if (value >= 0) {
		cbor_set_uint8(val, value);
	} else {
		cbor_set_uint8(val, 0xFF - value);
		cbor_mark_negint(val);
	}
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push s8 to indefinite map");
}

/**
 * fu_coswid_write_tag_item:
 * @item: a #cbor_item_t
 * @tag: a #FuCoswidTag, e.g. %FU_COSWID_TAG_PAYLOAD
 * @value: a #cbor_item_t
 *
 * Writes a raw tag value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
void
fu_coswid_write_tag_item(cbor_item_t *item, FuCoswidTag tag, cbor_item_t *value)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	if (!cbor_map_add(item, (struct cbor_pair){.key = key, .value = value}))
		g_critical("failed to push item to indefinite map");
}

/**
 * fu_coswid_parse_one_or_many:
 * @item: a #cbor_item_t
 * @func: a function to call with each map value
 * @user_data: pointer value to pass to @func
 * @error: (nullable): optional return location for an error
 *
 * Parses an item that *may* be an array, calling @func on each map value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.17
 **/
gboolean
fu_coswid_parse_one_or_many(cbor_item_t *item,
			    FuCoswidItemFunc func,
			    gpointer user_data,
			    GError **error)
{
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* one */
	if (cbor_isa_map(item))
		return func(item, user_data, error);

	/* many */
	if (cbor_isa_array(item)) {
		for (guint j = 0; j < cbor_array_size(item); j++) {
			g_autoptr(cbor_item_t) value = cbor_array_get(item, j);
			if (!cbor_isa_map(value)) {
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

#endif
