/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCbor"

#include "config.h"

#include "fu-cbor-common.h"
#include "fu-cbor-item-private.h"
#include "fu-input-stream.h"

typedef struct {
	guint max_depth;
	guint max_items;
	guint max_length;
	GInputStream *stream; /* no ref */
	gsize offset;
} FuCborParseHelper;

static FuCborItem *
fu_cbor_parse_item(FuCborParseHelper *helper,
		   guint current_depth,
		   GError **error);

static FuCborItem *
fu_cbor_parse_map(FuCborParseHelper *helper,
		  guint64 len,
		  guint current_depth,
		  GError **error)
{
	g_autoptr(FuCborItem) item = fu_cbor_item_new_map();

	/* sanity check */
	if (helper->max_depth > 0 && current_depth > helper->max_depth) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "reached max depth of %u",
			    current_depth);
		return NULL;
	}
	if (helper->max_items > 0 && len > helper->max_items) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "too many items (%u of maximum %u)",
			    (guint)len,
			    helper->max_items);
		return NULL;
	}

	g_debug("map has %u items", (guint)len);
	for (guint64 i = 0; i < len; i++) {
		g_autoptr(FuCborItem) item_key = NULL;
		g_autoptr(FuCborItem) item_val = NULL;
		item_key = fu_cbor_parse_item(helper, current_depth, error);
		if (item_key == NULL)
			return NULL;
		item_val = fu_cbor_parse_item(helper, current_depth, error);
		if (item_val == NULL)
			return NULL;
		if (!fu_cbor_item_map_append(item, item_key, item_val, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&item);
}

static FuCborItem *
fu_cbor_parse_array(FuCborParseHelper *helper,
		    guint64 len,
		    guint current_depth,
		    GError **error)
{
	g_autoptr(FuCborItem) item = fu_cbor_item_new_array();

	/* sanity check */
	if (helper->max_depth > 0 && current_depth > helper->max_depth) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "reached max depth of %u",
			    current_depth);
		return NULL;
	}
	if (helper->max_items > 0 && len > helper->max_items) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "too many items (%u of maximum %u)",
			    (guint)len,
			    helper->max_items);
		return NULL;
	}

	g_debug("array has %u items", (guint)len);
	for (guint64 i = 0; i < len; i++) {
		g_autoptr(FuCborItem) item_tmp = NULL;
		item_tmp = fu_cbor_parse_item(helper, current_depth, error);
		if (item_tmp == NULL)
			return NULL;
		if (!fu_cbor_item_array_append(item, item_tmp, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&item);
}

static FuCborItem *
fu_cbor_parse_item(FuCborParseHelper *helper,
		   guint current_depth,
		   GError **error)
{
	FuCborTag tag;
	guint64 len = 0;
	guint8 len_short;
	guint8 value8 = 0;

	if (!fu_input_stream_read_u8(helper->stream, helper->offset, &value8, error))
		return NULL;
	helper->offset += 1;
	tag = (value8 & 0b11100000) >> 5;
	g_debug("tag: %u [%s] @0x%x", tag, fu_cbor_tag_to_string(tag), (guint)helper->offset);

	/* process length */
	len_short = (guint)(value8 & 0b11111);
	g_debug("len-short: %u", len_short);
	if (len_short <= FU_CBOR_LEN_SHORT_MAX) {
		len = len_short;
	} else if (len_short == FU_CBOR_LEN_EXT8) {
		if (!fu_input_stream_read_u8(helper->stream, helper->offset, &value8, error))
			return NULL;
		len = value8;
		helper->offset += 1;
	} else if (len_short == FU_CBOR_LEN_EXT16) {
		guint16 value16 = 0;
		if (!fu_input_stream_read_u16(helper->stream,
					      helper->offset,
					      &value16,
					      G_BIG_ENDIAN,
					      error))
			return NULL;
		len = value16;
		helper->offset += 2;
	} else if (len_short == FU_CBOR_LEN_EXT32) {
		guint32 value32 = 0;
		if (!fu_input_stream_read_u32(helper->stream,
					      helper->offset,
					      &value32,
					      G_BIG_ENDIAN,
					      error))
			return NULL;
		len = value32;
		helper->offset += 4;
	} else if (len_short == FU_CBOR_LEN_EXT64) {
		guint64 value64 = 0;
		if (!fu_input_stream_read_u64(helper->stream,
					      helper->offset,
					      &value64,
					      G_BIG_ENDIAN,
					      error))
			return NULL;
		if (value64 > G_MAXINT64) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "lengths larger than s64:MAX are not supported");
			return NULL;
		}
		len = value64;
		helper->offset += 8;
	} else if (len_short == FU_CBOR_LEN_INDEFINITE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "indefinite-length encoding is not supported");
		return NULL;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "short count %u is invalid",
			    len_short);
		return NULL;
	}
	if (len != len_short)
		g_debug("len: %" G_GUINT64_FORMAT, len);

	/* process tags */
	if (tag == FU_CBOR_TAG_POS_INT)
		return fu_cbor_item_new_integer(len);
	if (tag == FU_CBOR_TAG_NEG_INT)
		return fu_cbor_item_new_integer(-1 - (gint64)len);
	if (tag == FU_CBOR_TAG_STRING) {
		g_autofree gchar *str = NULL;
		if (helper->max_length > 0 && len > helper->max_length) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "string too long (%u of maximum %u)",
				    (guint)len,
				    helper->max_length);
			return NULL;
		}
		str = fu_input_stream_read_string(helper->stream, helper->offset, len, error);
		if (str == NULL)
			return NULL;
		helper->offset += len;
		return fu_cbor_item_new_string_steal(g_steal_pointer(&str));
	}
	if (tag == FU_CBOR_TAG_BYTES) {
		g_autoptr(GBytes) blob = NULL;
		if (helper->max_length > 0 && len > helper->max_length) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "bytes too long (%u of maximum %u)",
				    (guint)len,
				    helper->max_length);
			return NULL;
		}
		blob = fu_input_stream_read_bytes(helper->stream, helper->offset, len, NULL, error);
		if (blob == NULL)
			return NULL;
		helper->offset += len;
		return fu_cbor_item_new_bytes(blob);
	}
	if (tag == FU_CBOR_TAG_SPECIAL) {
		if (len == FU_CBOR_SPECIAL_VALUE_TRUE)
			return fu_cbor_item_new_boolean(TRUE);
		if (len == FU_CBOR_SPECIAL_VALUE_FALSE)
			return fu_cbor_item_new_boolean(FALSE);
		if (len == FU_CBOR_SPECIAL_VALUE_NULL)
			return fu_cbor_item_new_string(NULL);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "special value %u [%s] is not handled",
			    (guint)len,
			    fu_cbor_special_value_to_string(len));
		return NULL;
	}
	if (tag == FU_CBOR_TAG_MAP)
		return fu_cbor_parse_map(helper, len, current_depth + 1, error);
	if (tag == FU_CBOR_TAG_ARRAY)
		return fu_cbor_parse_array(helper, len, current_depth + 1, error);

	/* unknown */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "tag %u [%s] is not handled",
		    tag,
		    fu_cbor_tag_to_string(tag));
	return NULL;
}

/**
 * fu_cbor_parse: (skip):
 * @stream: a #GInputStream
 * @offset: (inout) (nullable): stream position
 * @max_depth: maximum depth, or 0 for no limit
 * @max_items: maximum number of items, or 0 for no limit
 * @max_length: maximum length of strings and byte arrays, or 0 for no limit
 * @error: (nullable): optional return location for an error
 *
 * Parses a buffer into a CBOR map or array.
 *
 * Returns: (transfer full): root item, or %NULL on error
 *
 * Since: 2.1.2
 **/
FuCborItem *
fu_cbor_parse(GInputStream *stream,
	      gsize *offset,
	      guint max_depth,
	      guint max_items,
	      guint max_length,
	      GError **error)
{
	g_autoptr(FuCborItem) item = NULL;
	FuCborParseHelper helper = {
	    .stream = stream,
	    .max_depth = max_depth,
	    .max_items = max_items,
	    .max_length = max_length,
	};

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (offset != NULL)
		helper.offset = *offset;
	item = fu_cbor_parse_item(&helper, 0, error);
	if (item == NULL) {
		g_prefix_error(error, "CBOR parsing failed @0x%x: ", (guint)helper.offset);
		return NULL;
	}
	if (fu_cbor_item_get_kind(item) != FU_CBOR_ITEM_KIND_MAP &&
	    fu_cbor_item_get_kind(item) != FU_CBOR_ITEM_KIND_ARRAY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "CBOR data must start with array or map, got %s",
			    fu_cbor_item_kind_to_string(fu_cbor_item_get_kind(item)));
		return NULL;
	}

	/* success */
	if (offset != NULL)
		*offset = helper.offset;
	return g_steal_pointer(&item);
}
