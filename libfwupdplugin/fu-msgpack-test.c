/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_msgpack_lookup_func(void)
{
	g_autoptr(FuMsgpackItem) item1 = NULL;
	g_autoptr(FuMsgpackItem) item2 = NULL;
	g_autoptr(FuMsgpackItem) item3 = NULL;
	g_autoptr(FuMsgpackItem) item4 = NULL;
	g_autoptr(FuMsgpackItem) item5 = NULL;
	g_autoptr(FuMsgpackItem) item6 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GPtrArray) items_invalid =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	/* empty */
	item1 = fu_msgpack_map_lookup(items, 0, "foo", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(item1);
	g_clear_error(&error);

	/* map of stuff */
	g_ptr_array_add(items, fu_msgpack_item_new_string("offset"));
	g_ptr_array_add(items, fu_msgpack_item_new_map(2));
	g_ptr_array_add(items, fu_msgpack_item_new_string("fixint"));
	g_ptr_array_add(items, fu_msgpack_item_new_integer(6));
	g_ptr_array_add(items, fu_msgpack_item_new_string("uint8"));
	/* ...value is missing here */

	/* not a map */
	item2 = fu_msgpack_map_lookup(items, 0, "fixint", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(item2);
	g_clear_error(&error);

	/* items too small */
	item3 = fu_msgpack_map_lookup(items, 1, "fixint", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(item3);
	g_clear_error(&error);

	/* add the missing value */
	g_ptr_array_add(items, fu_msgpack_item_new_integer(256));

	/* get valid */
	item4 = fu_msgpack_map_lookup(items, 1, "fixint", &error);
	g_assert_no_error(error);
	g_assert_nonnull(item4);

	/* not found */
	item5 = fu_msgpack_map_lookup(items, 1, "not-going-to-exist", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(item5);
	g_clear_error(&error);

	/* not string key */
	g_ptr_array_add(items_invalid, fu_msgpack_item_new_map(1));
	g_ptr_array_add(items_invalid, fu_msgpack_item_new_integer(12));
	g_ptr_array_add(items_invalid, fu_msgpack_item_new_integer(34));

	/* get valid */
	item6 = fu_msgpack_map_lookup(items_invalid, 0, "fixint", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(item6);
	g_clear_error(&error);
}

static void
fu_msgpack_binary_stream_func(void)
{
	const gchar data[] = "hello";
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = g_bytes_new(data, sizeof(data));
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = G_INPUT_STREAM(g_memory_input_stream_new_from_bytes(blob));
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_ptr_array_add(items, fu_msgpack_item_new_binary_stream(stream));
	buf = fu_msgpack_write(items, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	fu_dump_raw(G_LOG_DOMAIN, "foo", buf->data, buf->len);
	g_assert_cmpint(buf->len, ==, 8);
	g_assert_cmpuint(buf->data[0], ==, FU_MSGPACK_CMD_BIN8);
	g_assert_cmpuint(buf->data[1], ==, sizeof(data));
	g_assert_cmpuint(buf->data[2], ==, 'h');
	g_assert_cmpuint(buf->data[3], ==, 'e');
	g_assert_cmpuint(buf->data[4], ==, 'l');
	g_assert_cmpuint(buf->data[5], ==, 'l');
	g_assert_cmpuint(buf->data[6], ==, 'o');
	g_assert_cmpuint(buf->data[7], ==, '\0');
}

static void
fu_msgpack_parse_binary_func(void)
{
	/* 64 bit float 100.0099 */
	const guchar data[] = {0xCB, 0x40, 0x59, 0x00, 0xA2, 0x33, 0x9C, 0x0E, 0xBF};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_byte_array_append(buf, data, sizeof(data));

	items = fu_msgpack_parse(buf, &error);
	g_assert_no_error(error);
	g_assert_nonnull(items);
	g_assert_cmpint(items->len, ==, 1);

	g_assert_cmpfloat_with_epsilon(fu_msgpack_item_get_float(g_ptr_array_index(items, 0)),
				       100.0099,
				       0.00001);
}

static void
fu_msgpack_func(void)
{
	g_autoptr(GByteArray) buf1 = NULL;
	g_autoptr(GByteArray) buf2 = NULL;
	g_autoptr(GByteArray) buf_in = g_byte_array_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) items_new = NULL;
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	FuMsgpackItemKind kinds[] = {
	    FU_MSGPACK_ITEM_KIND_MAP,
	    FU_MSGPACK_ITEM_KIND_STRING,
	    FU_MSGPACK_ITEM_KIND_INTEGER,
	    FU_MSGPACK_ITEM_KIND_STRING,
	    FU_MSGPACK_ITEM_KIND_INTEGER,
	    FU_MSGPACK_ITEM_KIND_STRING,
	    FU_MSGPACK_ITEM_KIND_FLOAT,
	    FU_MSGPACK_ITEM_KIND_STRING,
	    FU_MSGPACK_ITEM_KIND_ARRAY,
	    FU_MSGPACK_ITEM_KIND_BINARY,
	};

	/* empty */
	buf1 = fu_msgpack_write(items, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf1);
	g_assert_cmpint(buf1->len, ==, 0);

	/* prepare */
	fu_byte_array_append_uint24(buf_in, 0x1234, G_LITTLE_ENDIAN);

	/* map of stuff */
	g_ptr_array_add(items, fu_msgpack_item_new_map(4));
	g_ptr_array_add(items, fu_msgpack_item_new_string("fixint"));
	g_ptr_array_add(items, fu_msgpack_item_new_integer(6));
	g_ptr_array_add(items, fu_msgpack_item_new_string("uint8"));
	g_ptr_array_add(items, fu_msgpack_item_new_integer(256));
	g_ptr_array_add(items, fu_msgpack_item_new_string("float"));
	g_ptr_array_add(items, fu_msgpack_item_new_float(1.0));
	g_ptr_array_add(items, fu_msgpack_item_new_string("array-of-data"));
	g_ptr_array_add(items, fu_msgpack_item_new_array(1));
	g_ptr_array_add(items, fu_msgpack_item_new_binary(buf_in));
	buf2 = fu_msgpack_write(items, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf2);
	g_assert_cmpint(buf2->len, ==, 53);

	/* parse it back */
	items_new = fu_msgpack_parse(buf2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(items_new);
	g_assert_cmpint(items_new->len, ==, 10);

	for (guint i = 0; i < G_N_ELEMENTS(kinds); i++) {
		FuMsgpackItem *item = g_ptr_array_index(items_new, i);
		g_assert_cmpint(fu_msgpack_item_get_kind(item), ==, kinds[i]);
	}
	g_assert_cmpint(fu_msgpack_item_get_map(g_ptr_array_index(items_new, 0)), ==, 4);
	g_assert_cmpint(fu_msgpack_item_get_integer(g_ptr_array_index(items_new, 2)), ==, 6);
	g_assert_cmpint(fu_msgpack_item_get_integer(g_ptr_array_index(items_new, 4)), ==, 256);
	g_assert_cmpfloat_with_epsilon(fu_msgpack_item_get_float(g_ptr_array_index(items_new, 6)),
				       1.0,
				       0.00001);
	g_assert_cmpint(fu_msgpack_item_get_array(g_ptr_array_index(items_new, 8)), ==, 1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/msgpack", fu_msgpack_func);
	g_test_add_func("/fwupd/msgpack/binary-stream", fu_msgpack_binary_stream_func);
	g_test_add_func("/fwupd/msgpack/parse-binary", fu_msgpack_parse_binary_func);
	g_test_add_func("/fwupd/msgpack/lookup", fu_msgpack_lookup_func);
	return g_test_run();
}
