/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cbor-common.h"

/* nocheck:magic-inlines=200 */

static void
fu_cbor_item_depth_func(void)
{
	gboolean ret;
	g_autoptr(FuCborItem) item = NULL;
	g_autoptr(FuCborItem) item1 = fu_cbor_item_new_array();
	g_autoptr(FuCborItem) item2 = fu_cbor_item_new_array();
	g_autoptr(FuCborItem) item3 = fu_cbor_item_new_array();
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	ret = fu_cbor_item_array_append(item1, item2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_cbor_item_array_append(item2, item3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	buf = fu_cbor_item_write(item1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 3);

	stream = g_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	g_assert_nonnull(stream);
	item = fu_cbor_parse(stream, NULL, 2, 0, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(item);
}

static void
fu_cbor_item_items_func(void)
{
	gboolean ret;
	g_autoptr(FuCborItem) item = NULL;
	g_autoptr(FuCborItem) item1 = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) item2 = fu_cbor_item_new_integer(1);
	g_autoptr(FuCborItem) item3 = fu_cbor_item_new_integer(2);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	ret = fu_cbor_item_map_append(item1, item2, item3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_cbor_item_map_append(item1, item2, item3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	buf = fu_cbor_item_write(item1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 5);

	stream = g_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	g_assert_nonnull(stream);
	item = fu_cbor_parse(stream, NULL, 0, 1, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(item);
}

static void
fu_cbor_item_boolean_func(void)
{
	gboolean ret;
	gboolean value = TRUE;
	g_autofree gchar *str = NULL;
	g_autoptr(FuCborItem) item = fu_cbor_item_new_boolean(FALSE);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_cbor_item_get_boolean(item, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(value);

	/* wrong type */
	ret = fu_cbor_item_get_integer(item, NULL, NULL);
	g_assert_false(ret);
	str = fu_cbor_item_get_string(item, NULL);
	g_assert_null(str);
	blob = fu_cbor_item_get_bytes(item, NULL);
	g_assert_null(blob);

	/* not in an an array or map */
	buf = fu_cbor_item_write(item, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(buf);
}

static void
fu_cbor_item_integer_func(void)
{
	gboolean ret;
	g_autoptr(FuCborItem) item = NULL;
	g_autoptr(FuCborItem) item1 = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) item2 = fu_cbor_item_new_integer(G_MAXINT32);
	g_autoptr(FuCborItem) item3 = fu_cbor_item_new_integer(G_MAXINT64);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	ret = fu_cbor_item_map_append(item1, item2, item3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_cbor_item_map_append(item1, item2, item3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	buf = fu_cbor_item_write(item1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 29);

	stream = g_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	g_assert_nonnull(stream);
	item = fu_cbor_parse(stream, NULL, 0, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(item);
}

static void
fu_cbor_item_string_func(void)
{
	FuCborItem *item_tmp;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuCborItem) item = NULL;
	g_autoptr(FuCborItem) item1 = fu_cbor_item_new_array();
	g_autoptr(FuCborItem) item2 = fu_cbor_item_new_string(NULL);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	ret = fu_cbor_item_array_append(item1, item2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	buf = fu_cbor_item_write(item1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 2);

	stream = g_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	g_assert_nonnull(stream);
	item = fu_cbor_parse(stream, NULL, 0, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(item);
	g_assert_cmpint(fu_cbor_item_array_length(item), ==, 1);
	item_tmp = fu_cbor_item_array_index(item, 0);
	g_assert_nonnull(item_tmp);
	str = fu_cbor_item_get_string(item_tmp, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(str);
}

static void
fu_cbor_item_bytes_func(void)
{
	FuCborItem *item_tmp;
	gboolean ret;
	g_autoptr(GBytes) blob = g_bytes_new_static("buf", 3);
	g_autoptr(GBytes) blob2 = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuCborItem) item = NULL;
	g_autoptr(FuCborItem) item1 = fu_cbor_item_new_array();
	g_autoptr(FuCborItem) item2 = fu_cbor_item_new_bytes(blob);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	ret = fu_cbor_item_array_append(item1, item2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	buf = fu_cbor_item_write(item1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 5);

	stream = g_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
	g_assert_nonnull(stream);
	item = fu_cbor_parse(stream, NULL, 0, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(item);
	g_assert_cmpint(fu_cbor_item_array_length(item), ==, 1);
	item_tmp = fu_cbor_item_array_index(item, 0);
	g_assert_nonnull(item_tmp);
	blob2 = fu_cbor_item_get_bytes(item_tmp, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 3);

	/* blob to string */
	str = fu_cbor_item_to_string(item_tmp);
	g_assert_cmpstr(str, ==, "0x627566");
}

static void
fu_cbor_item_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuCborItem) item = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	const guint8 buf[] = {
	    0xa7, 0x0f, 0x65, 0x65, 0x6e, 0x2d, 0x55, 0x53, 0x00, 0x64, 0x64, 0x61, 0x76, 0x65,
	    0x08, 0xf5, 0x01, 0x6c, 0x66, 0x69, 0x72, 0x6d, 0x77, 0x61, 0x72, 0x65, 0x2e, 0x62,
	    0x69, 0x6e, 0x0d, 0x65, 0x31, 0x2e, 0x32, 0x2e, 0x33, 0x05, 0xa4, 0x18, 0x32, 0x65,
	    0x75, 0x53, 0x57, 0x49, 0x44, 0x18, 0x36, 0x63, 0x64, 0x65, 0x66, 0x18, 0x34, 0x64,
	    0x44, 0x61, 0x76, 0x65, 0x18, 0x2d, 0x63, 0x61, 0x63, 0x62, 0x02, 0xa3, 0x18, 0x1f,
	    0x6b, 0x48, 0x75, 0x67, 0x68, 0x73, 0x6b, 0x69, 0x20, 0x4c, 0x74, 0x64, 0x18, 0x20,
	    0x6b, 0x68, 0x75, 0x67, 0x68, 0x73, 0x69, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x18, 0x21,
	    0x86, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	};

	/*
	 * {"15": "en-US", "0": "dave", "8": true, "1": "firmware.bin", "13": "1.2.3", "5": {"50":
	 * "uSWID", "54": "def", "52": "Dave", "45": "acb"}, "2": {"31": "Hughski Ltd", "32":
	 * "hughsie.com", "33": [1, 2, 3, 4, 5, 6]}}
	 */
	stream = g_memory_input_stream_new_from_data(buf, sizeof(buf), NULL);
	g_assert_nonnull(stream);

	item = fu_cbor_parse(stream, NULL, 0, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(item);

	str = fu_cbor_item_to_string(item);
	g_debug("%s", str);
	g_assert_cmpstr(str,
			==,
			"{15: \"en-US\", 0: \"dave\", 8: True, 1: \"firmware.bin\", 13: \"1.2.3\", "
			"5: {50: \"uSWID\", 54: \"def\", 52: \"Dave\", 45: \"acb\"}, 2: {31: "
			"\"Hughski Ltd\", 32: \"hughsie.com\", 33: [1, 2, 3, 4, 5, 6]}}");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/cbor-item", fu_cbor_item_func);
	g_test_add_func("/fwupd/cbor-item/depth", fu_cbor_item_depth_func);
	g_test_add_func("/fwupd/cbor-item/items", fu_cbor_item_items_func);
	g_test_add_func("/fwupd/cbor-item/boolean", fu_cbor_item_boolean_func);
	g_test_add_func("/fwupd/cbor-item/string", fu_cbor_item_string_func);
	g_test_add_func("/fwupd/cbor-item/integer", fu_cbor_item_integer_func);
	g_test_add_func("/fwupd/cbor-item/bytes", fu_cbor_item_bytes_func);
	return g_test_run();
}
