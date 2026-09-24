/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCbor"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-cbor-common.h"
#include "fu-cbor-item-private.h"
#include "fu-common.h"
#include "fu-input-stream.h"

typedef struct {
	guint max_depth;
	guint max_items;
	guint max_length;
	FuInputStream *stream; /* no ref */
	gsize offset;
} FuCborParseHelper;

static FuCborItem *
fu_cbor_parse_item(FuCborParseHelper *helper,
		   guint current_depth,
		   gboolean forbid_indefinite,
		   gboolean *got_break,
		   GError **error);

static FuCborItem *
fu_cbor_parse_map(FuCborParseHelper *helper,
		  guint64 len,
		  FuCborMode mode,
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
	if (mode == FU_CBOR_MODE_DEFINITE && helper->max_items > 0 && len > helper->max_items) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "too many items (%u of maximum %u)",
			    (guint)len,
			    helper->max_items);
		return NULL;
	}

	g_debug("map has %s items", fu_cbor_mode_to_string(mode));
	for (guint64 i = 0; mode == FU_CBOR_MODE_INDEFINITE || i < len; i++) {
		gboolean got_break = FALSE;
		g_autoptr(FuCborItem) item_key = NULL;
		g_autoptr(FuCborItem) item_val = NULL;

		item_key = fu_cbor_parse_item(helper,
					      current_depth,
					      FALSE,
					      mode == FU_CBOR_MODE_INDEFINITE ? &got_break : NULL,
					      error);
		if (item_key == NULL) {
			if (got_break)
				break;
			return NULL;
		}

		/* the item count is unknown ahead of time, so limit incrementally; this is
		 * checked after the terminating break so that exactly @max_items is allowed */
		if (mode == FU_CBOR_MODE_INDEFINITE && helper->max_items > 0 &&
		    i >= helper->max_items) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many items (maximum %u)",
				    helper->max_items);
			return NULL;
		}

		/* a break between a key and value is a truncated map */
		item_val = fu_cbor_parse_item(helper, current_depth, FALSE, NULL, error);
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
		    FuCborMode mode,
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
	if (mode == FU_CBOR_MODE_DEFINITE && helper->max_items > 0 && len > helper->max_items) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "too many items (%u of maximum %u)",
			    (guint)len,
			    helper->max_items);
		return NULL;
	}

	g_debug("array has %s items", fu_cbor_mode_to_string(mode));
	for (guint64 i = 0; mode == FU_CBOR_MODE_INDEFINITE || i < len; i++) {
		gboolean got_break = FALSE;
		g_autoptr(FuCborItem) item_tmp = NULL;

		item_tmp = fu_cbor_parse_item(helper,
					      current_depth,
					      FALSE,
					      mode == FU_CBOR_MODE_INDEFINITE ? &got_break : NULL,
					      error);
		if (item_tmp == NULL) {
			if (got_break)
				break;
			return NULL;
		}

		/* the item count is unknown ahead of time, so limit incrementally; this is
		 * checked after the terminating break so that exactly @max_items is allowed */
		if (mode == FU_CBOR_MODE_INDEFINITE && helper->max_items > 0 &&
		    i >= helper->max_items) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many items (maximum %u)",
				    helper->max_items);
			return NULL;
		}
		if (!fu_cbor_item_array_append(item, item_tmp, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&item);
}

static FuCborItem *
fu_cbor_parse_string_indefinite(FuCborParseHelper *helper, guint current_depth, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* concatenate definite-length string chunks until a break stop-code */
	for (;;) {
		gboolean got_break = FALSE;
		g_autofree gchar *chunk = NULL;
		g_autoptr(FuCborItem) item_tmp = NULL;

		item_tmp = fu_cbor_parse_item(helper, current_depth, TRUE, &got_break, error);
		if (item_tmp == NULL) {
			if (got_break)
				break;
			return NULL;
		}
		if (fu_cbor_item_get_kind(item_tmp) != FU_CBOR_ITEM_KIND_STRING) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "indefinite-length string chunk must be a string, got %s",
				    fu_cbor_item_kind_to_string(fu_cbor_item_get_kind(item_tmp)));
			return NULL;
		}
		chunk = fu_cbor_item_get_string(item_tmp, error);
		if (chunk == NULL)
			return NULL;
		g_string_append(str, chunk);
		if (helper->max_length > 0 && str->len > helper->max_length) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "string too long (%u of maximum %u)",
				    (guint)str->len,
				    helper->max_length);
			return NULL;
		}
	}

	/* success */
	return fu_cbor_item_new_string_steal(g_string_free_and_steal(g_steal_pointer(&str)));
}

static FuCborItem *
fu_cbor_parse_bytes_indefinite(FuCborParseHelper *helper, guint current_depth, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* concatenate definite-length byte-string chunks until a break stop-code */
	for (;;) {
		gboolean got_break = FALSE;
		g_autoptr(FuCborItem) item_tmp = NULL;
		g_autoptr(GBytes) chunk = NULL;

		item_tmp = fu_cbor_parse_item(helper, current_depth, TRUE, &got_break, error);
		if (item_tmp == NULL) {
			if (got_break)
				break;
			return NULL;
		}
		if (fu_cbor_item_get_kind(item_tmp) != FU_CBOR_ITEM_KIND_BYTES) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "indefinite-length byte-string chunk must be bytes, got %s",
				    fu_cbor_item_kind_to_string(fu_cbor_item_get_kind(item_tmp)));
			return NULL;
		}
		chunk = fu_cbor_item_get_bytes(item_tmp, error);
		if (chunk == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, chunk);
		if (helper->max_length > 0 && buf->len > helper->max_length) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "bytes too long (%u of maximum %u)",
				    buf->len,
				    helper->max_length);
			return NULL;
		}
	}

	/* success */
	blob = g_bytes_new(buf->data, buf->len);
	return fu_cbor_item_new_bytes(blob);
}

static FuCborItem *
fu_cbor_parse_item(FuCborParseHelper *helper,
		   guint current_depth,
		   gboolean forbid_indefinite,
		   gboolean *got_break,
		   GError **error)
{
	FuCborTag tag;
	FuCborMode mode = FU_CBOR_MODE_DEFINITE;
	guint64 len = 0;
	guint8 len_short;
	guint8 value8 = 0;

	if (!fu_input_stream_read_u8(helper->stream, helper->offset, &value8, error))
		return NULL;

	if (!fu_size_checked_inc(&helper->offset, 1, error)) {
		g_prefix_error_literal(error, "CBOR tag offset overflow: ");
		return NULL;
	}

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

		if (!fu_size_checked_inc(&helper->offset, 1, error)) {
			g_prefix_error_literal(error, "CBOR length8 offset overflow: ");
			return NULL;
		}
	} else if (len_short == FU_CBOR_LEN_EXT16) {
		guint16 value16 = 0;
		if (!fu_input_stream_read_u16(helper->stream,
					      helper->offset,
					      &value16,
					      G_BIG_ENDIAN,
					      error))
			return NULL;
		len = value16;

		if (!fu_size_checked_inc(&helper->offset, 2, error)) {
			g_prefix_error_literal(error, "CBOR length16 offset overflow: ");
			return NULL;
		}
	} else if (len_short == FU_CBOR_LEN_EXT32) {
		guint32 value32 = 0;
		if (!fu_input_stream_read_u32(helper->stream,
					      helper->offset,
					      &value32,
					      G_BIG_ENDIAN,
					      error))
			return NULL;
		len = value32;

		if (!fu_size_checked_inc(&helper->offset, 4, error)) {
			g_prefix_error_literal(error, "CBOR length32 offset overflow: ");
			return NULL;
		}
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

		if (!fu_size_checked_inc(&helper->offset, 8, error)) {
			g_prefix_error_literal(error, "CBOR length64 offset overflow: ");
			return NULL;
		}
	} else if (len_short == FU_CBOR_LEN_INDEFINITE) {
		mode = FU_CBOR_MODE_INDEFINITE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "short count %u is invalid",
			    len_short);
		return NULL;
	}
	if (mode == FU_CBOR_MODE_DEFINITE && len != len_short)
		g_debug("len: %" G_GUINT64_FORMAT, len);

	/*
	 * indefinite length is only valid for byte strings, text strings, arrays and maps -- for
	 * the special major type it is the break stop-code, handled below
	 */
	if (mode == FU_CBOR_MODE_INDEFINITE && tag != FU_CBOR_TAG_STRING &&
	    tag != FU_CBOR_TAG_BYTES && tag != FU_CBOR_TAG_ARRAY && tag != FU_CBOR_TAG_MAP &&
	    tag != FU_CBOR_TAG_SPECIAL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "indefinite length is not valid for tag %s",
			    fu_cbor_tag_to_string(tag));
		return NULL;
	}

	/* chunks of an indefinite-length string must themselves be definite-length */
	if (mode == FU_CBOR_MODE_INDEFINITE && forbid_indefinite && tag != FU_CBOR_TAG_SPECIAL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "nested indefinite-length encoding is not allowed");
		return NULL;
	}

	/* process tags */
	if (tag == FU_CBOR_TAG_POS_INT)
		return fu_cbor_item_new_integer(len);
	if (tag == FU_CBOR_TAG_NEG_INT)
		return fu_cbor_item_new_integer(-1 - (gint64)len);
	if (tag == FU_CBOR_TAG_STRING) {
		g_autofree gchar *str = NULL;
		if (mode == FU_CBOR_MODE_INDEFINITE)
			return fu_cbor_parse_string_indefinite(helper, current_depth, error);
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
		if (!fu_size_checked_inc(&helper->offset, len, error))
			return NULL;
		return fu_cbor_item_new_string_steal(g_steal_pointer(&str));
	}
	if (tag == FU_CBOR_TAG_BYTES) {
		g_autoptr(GBytes) blob = NULL;
		if (mode == FU_CBOR_MODE_INDEFINITE)
			return fu_cbor_parse_bytes_indefinite(helper, current_depth, error);
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
		if (!fu_size_checked_inc(&helper->offset, len, error))
			return NULL;
		return fu_cbor_item_new_bytes(blob);
	}
	if (tag == FU_CBOR_TAG_SPECIAL) {
		if (mode == FU_CBOR_MODE_INDEFINITE) {
			/* this is the break stop-code that terminates indefinite lengths */
			if (got_break != NULL) {
				*got_break = TRUE;
				return NULL;
			}
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "unexpected break stop-code");
			return NULL;
		}
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
		return fu_cbor_parse_map(helper, len, mode, current_depth + 1, error);
	if (tag == FU_CBOR_TAG_ARRAY)
		return fu_cbor_parse_array(helper, len, mode, current_depth + 1, error);

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
 * @stream: a #FuInputStream
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
fu_cbor_parse(FuInputStream *stream,
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

	g_return_val_if_fail(FU_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (offset != NULL)
		helper.offset = *offset;
	item = fu_cbor_parse_item(&helper, 0, FALSE, NULL, error);
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
