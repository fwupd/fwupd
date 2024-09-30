/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#define G_LOG_DOMAIN "FuSelfTest"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <string.h>

#include "fwupd-security-attr-private.h"

#include "fu-backend-private.h"
#include "fu-bios-settings-private.h"
#include "fu-common-private.h"
#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-coswid-firmware.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-device-progress.h"
#include "fu-dummy-efivars.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efivars-private.h"
#include "fu-lzma-common.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-self-test-struct.h"
#include "fu-smbios-private.h"
#include "fu-test-device.h"
#include "fu-volume-private.h"

static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
fu_test_hang_check_cb(gpointer user_data)
{
	g_main_loop_quit(_test_loop);
	_test_loop_timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_test_loop_run_with_timeout(guint timeout_ms)
{
	g_assert_cmpint(_test_loop_timeout_id, ==, 0);
	g_assert_null(_test_loop);
	_test_loop = g_main_loop_new(NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add(timeout_ms, fu_test_hang_check_cb, NULL);
	g_main_loop_run(_test_loop);
}

static void
fu_test_loop_quit(void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove(_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit(_test_loop);
		g_main_loop_unref(_test_loop);
		_test_loop = NULL;
	}
}

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
	g_assert_cmpint(fu_msgpack_item_get_array(g_ptr_array_index(items_new, 8)), ==, 1);
}

static void
fu_archive_invalid_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(archive);
}

static void
fu_archive_cab_func(void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) data_tmp1 = NULL;
	g_autoptr(GBytes) data_tmp2 = NULL;
	g_autoptr(GBytes) data_tmp3 = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "colorhug",
					 "colorhug-als-3.0.2.cab",
					 NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(archive);

	data_tmp1 = fu_archive_lookup_by_fn(archive, "firmware.metainfo.xml", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp1);
	checksum1 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp1);
	g_assert_cmpstr(checksum1, ==, "f62ee340c27bbb80229c3dd3cb2e78bddfc82d4f");

	data_tmp2 = fu_archive_lookup_by_fn(archive, "firmware.txt", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp2);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp2);
	g_assert_cmpstr(checksum2, ==, "22596363b3de40b06f981fb85d82312e8c0ed511");

	data_tmp3 = fu_archive_lookup_by_fn(archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(data_tmp3);
}

static void
fu_volume_gpt_type_func(void)
{
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0xef"),
			==,
			"c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0x0b"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("fat32lba"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0x00"), ==, "0x00");
}

static void
fu_common_align_up_func(void)
{
	g_assert_cmpint(fu_common_align_up(0, 0), ==, 0);
	g_assert_cmpint(fu_common_align_up(5, 0), ==, 5);
	g_assert_cmpint(fu_common_align_up(5, 3), ==, 8);
	g_assert_cmpint(fu_common_align_up(1023, 10), ==, 1024);
	g_assert_cmpint(fu_common_align_up(1024, 10), ==, 1024);
	g_assert_cmpint(fu_common_align_up(G_MAXSIZE - 1, 10), ==, G_MAXSIZE);
}

static void
fu_common_bitwise_func(void)
{
	guint64 val = 0;

	g_assert_true(FU_BIT_IS_CLEAR(val, 1));
	g_assert_true(FU_BIT_IS_CLEAR(val, 63));
	g_assert_false(FU_BIT_IS_SET(val, 1));
	g_assert_false(FU_BIT_IS_SET(val, 63));

	FU_BIT_SET(val, 1);
	FU_BIT_SET(val, 63);
	g_assert_true(FU_BIT_IS_SET(val, 1));
	g_assert_true(FU_BIT_IS_SET(val, 63));
	g_assert_cmpint(val, ==, 0x8000000000000002ull);

	FU_BIT_CLEAR(val, 1);
	FU_BIT_CLEAR(val, 63);
	g_assert_cmpint(val, ==, 0);
}

static void
fu_common_byte_array_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) array = g_byte_array_new();
	g_autoptr(GByteArray) array2 = NULL;
	g_autoptr(GByteArray) array3 = NULL;
	g_autoptr(GError) error = NULL;

	fu_byte_array_append_uint8(array, (guint8)'h');
	fu_byte_array_append_uint8(array, (guint8)'e');
	fu_byte_array_append_uint8(array, (guint8)'l');
	fu_byte_array_append_uint8(array, (guint8)'l');
	fu_byte_array_append_uint8(array, (guint8)'o');
	g_assert_cmpint(array->len, ==, 5);
	g_assert_cmpint(memcmp(array->data, "hello", array->len), ==, 0);

	fu_byte_array_set_size(array, 10, 0x00);
	g_assert_cmpint(array->len, ==, 10);
	g_assert_cmpint(memcmp(array->data, "hello\0\0\0\0\0", array->len), ==, 0);

	str = fu_byte_array_to_string(array);
	g_assert_cmpstr(str, ==, "68656c6c6f0000000000");

	array2 = fu_byte_array_from_string(str, &error);
	g_assert_no_error(error);
	g_assert_nonnull(array2);
	g_assert_cmpint(array2->len, ==, 10);
	g_assert_cmpint(memcmp(array2->data, "hello\0\0\0\0\0", array2->len), ==, 0);

	array3 = fu_byte_array_from_string("ZZZ", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(array3);
}

static void
fu_common_crc_func(void)
{
	guint8 buf[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};

	g_assert_cmpint(fu_crc8(FU_CRC_KIND_B8_STANDARD, buf, sizeof(buf)), ==, (guint8)~0x7A);
	g_assert_cmpint(fu_crc16(FU_CRC_KIND_B16_USB, buf, sizeof(buf)), ==, 0x4DF1);
	g_assert_cmpint(fu_crc_misr16(0, buf, (sizeof(buf) / 2) * 2), ==, 0x40D);
	g_assert_cmpint(fu_crc_misr16(0xFFFF, buf, (sizeof(buf) / 2) * 2), ==, 0xFBFA);

	/* all the CRC32 variants, verified using https://crccalc.com/?method=CRC-32 */
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_STANDARD, buf, sizeof(buf)), ==, 0x40EFAB9E);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_BZIP2, buf, sizeof(buf)), ==, 0x89AE7A5C);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_JAMCRC, buf, sizeof(buf)), ==, 0xBF105461);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_MPEG2, buf, sizeof(buf)), ==, 0x765185A3);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_POSIX, buf, sizeof(buf)), ==, 0x037915C4);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_SATA, buf, sizeof(buf)), ==, 0xBA55CCAC);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_XFER, buf, sizeof(buf)), ==, 0x868E70FC);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_C, buf, sizeof(buf)), ==, 0x5A14B9F9);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_D, buf, sizeof(buf)), ==, 0x68AD8D3C);
	g_assert_cmpint(fu_crc32(FU_CRC_KIND_B32_Q, buf, sizeof(buf)), ==, 0xE955C875);
}

static void
fu_string_append_func(void)
{
	g_autoptr(GString) str = g_string_new(NULL);
	fwupd_codec_string_append(str, 0, "hdr", "");
	fwupd_codec_string_append(str, 0, "key", "value");
	fwupd_codec_string_append(str, 0, "key1", "value1");
	fwupd_codec_string_append(str, 1, "key2", "value2");
	fwupd_codec_string_append(str, 1, "", "value2");
	fwupd_codec_string_append(str, 2, "key3", "value3");
	g_assert_cmpstr(str->str,
			==,
			"hdr:\n"
			"key:                    value\n"
			"key1:                   value1\n"
			"  key2:                 value2\n"
			"                        value2\n"
			"    key3:               value3\n");
}

static void
fu_version_guess_format_func(void)
{
	g_assert_cmpint(fu_version_guess_format(NULL), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format(""), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format("1234ac"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint(fu_version_guess_format("1.2"), ==, FWUPD_VERSION_FORMAT_PAIR);
	g_assert_cmpint(fu_version_guess_format("1.2.3"), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpint(fu_version_guess_format("1.2.3.4"), ==, FWUPD_VERSION_FORMAT_QUAD);
	g_assert_cmpint(fu_version_guess_format("1.2.3.4.5"), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format("1a.2b.3"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint(fu_version_guess_format("1"), ==, FWUPD_VERSION_FORMAT_NUMBER);
	g_assert_cmpint(fu_version_guess_format("0x10201"), ==, FWUPD_VERSION_FORMAT_NUMBER);
}

static void
fu_device_version_format_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "Ver1.2.3 RELEASE");
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
}

static void
fu_device_open_refcount_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	fu_device_set_id(device, "test_device");
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_false(ret);
}

static void
fu_device_name_func(void)
{
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);

	/* vendor then name */
	fu_device_set_vendor(device1, "  Hughski  ");
	fu_device_set_name(device1, "HUGHSKI  ColorHug(TM)__Pro  ");
	g_assert_cmpstr(fu_device_get_vendor(device1), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device1), ==, "ColorHug™ Pro");

	/* name then vendor */
	fu_device_set_name(device2, "Hughski ColorHug(TM)_Pro");
	fu_device_set_vendor(device2, "Hughski");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "ColorHug™ Pro");

	/* a real example */
	fu_device_set_name(device2, "Intel(R) Core(TM) i7-10850H CPU @ 2.70GHz");
	fu_device_set_vendor(device2, "Intel");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "Core™ i7-10850H CPU @ 2.70GHz");

	/* name and vendor are the same */
#ifndef SUPPORTED_BUILD
	g_test_expect_message("FuDevice", G_LOG_LEVEL_WARNING, "name and vendor are the same*");
#endif
	fu_device_set_name(device2, "example");
	fu_device_set_vendor(device2, "EXAMPLE");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "example");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "EXAMPLE");
}

static void
fu_device_cfi_device_func(void)
{
	gboolean ret;
	guint8 cmd = 0;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuCfiDevice) cfi_device = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	cfi_device = fu_cfi_device_new(ctx, "3730");
	ret = fu_device_setup(FU_DEVICE(cfi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* fallback */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_READ_DATA, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0x03);

	/* from quirk */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_CHIP_ERASE, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0xC7);
	g_assert_cmpint(fu_cfi_device_get_size(cfi_device), ==, 0x10000);
	g_assert_cmpint(fu_cfi_device_get_page_size(cfi_device), ==, 0x200);
	g_assert_cmpint(fu_cfi_device_get_sector_size(cfi_device), ==, 0x2000);
	g_assert_cmpint(fu_cfi_device_get_block_size(cfi_device), ==, 0x8000);
}

static void
fu_device_metadata_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	/* string */
	fu_device_set_metadata(device, "foo", "bar");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "bar");
	fu_device_set_metadata(device, "foo", "baz");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "baz");
	g_assert_null(fu_device_get_metadata(device, "unknown"));

	/* boolean */
	fu_device_set_metadata_boolean(device, "baz", TRUE);
	g_assert_cmpstr(fu_device_get_metadata(device, "baz"), ==, "true");
	g_assert_true(fu_device_get_metadata_boolean(device, "baz"));
	g_assert_false(fu_device_get_metadata_boolean(device, "unknown"));

	/* integer */
	fu_device_set_metadata_integer(device, "bam", 12345);
	g_assert_cmpstr(fu_device_get_metadata(device, "bam"), ==, "12345");
	g_assert_cmpint(fu_device_get_metadata_integer(device, "bam"), ==, 12345);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "unknown"), ==, G_MAXUINT);
}

static void
fu_string_utf16_func(void)
{
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;

	buf = fu_utf8_to_utf16_byte_array("hello world",
					  G_LITTLE_ENDIAN,
					  FU_UTF_CONVERT_FLAG_APPEND_NUL,
					  &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 24);
	g_assert_cmpint(buf->data[0], ==, 'h');
	g_assert_cmpint(buf->data[1], ==, '\0');
	g_assert_cmpint(buf->data[2], ==, 'e');
	g_assert_cmpint(buf->data[3], ==, '\0');
	str1 = fu_utf16_to_utf8_byte_array(buf, G_LITTLE_ENDIAN, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str1, ==, "hello world");

	/* failure */
	g_byte_array_set_size(buf, buf->len - 1);
	str2 = fu_utf16_to_utf8_byte_array(buf, G_LITTLE_ENDIAN, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpstr(str2, ==, NULL);
}

static void
fu_smbios_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* these tests will not write */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);

	smbios = fu_smbios_new();
	ret = fu_smbios_setup(smbios, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	g_debug("%s", dump);

	/* test for missing table */
	str = fu_smbios_get_string(smbios, 0xff, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* check for invalid offset */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0xff, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "LENOVO");
}

static void
fu_kernel_cmdline_func(void)
{
	const gchar *buf = "key=val foo bar=\"baz baz baz\" tail\n";
	g_autoptr(GHashTable) hash = NULL;

	hash = fu_kernel_parse_cmdline(buf, strlen(buf));
	g_assert_nonnull(hash);
	g_assert_true(g_hash_table_contains(hash, "key"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "key"), ==, "val");
	g_assert_true(g_hash_table_contains(hash, "foo"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "foo"), ==, NULL);
	g_assert_true(g_hash_table_contains(hash, "bar"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "bar"), ==, "baz baz baz");
	g_assert_true(g_hash_table_contains(hash, "tail"));
	g_assert_false(g_hash_table_contains(hash, ""));
}

static void
fu_kernel_config_func(void)
{
	const gchar *buf = "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE=y\n\n"
			   "# CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY is not set\n";
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GError) error = NULL;

	hash = fu_kernel_parse_config(buf, strlen(buf), &error);
	g_assert_no_error(error);
	g_assert_nonnull(hash);
	g_assert_true(g_hash_table_contains(hash, "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE"), ==, "y");
	g_assert_false(g_hash_table_contains(hash, "CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY"));
}

static void
fu_smbios3_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables64", NULL);
	smbios = fu_smbios_new();
	ret = fu_smbios_setup_from_path(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	g_debug("%s", dump);

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "Dell Inc.");
}

static void
fu_context_backends_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend2 = NULL;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, "name", "dummy", NULL);
	g_autoptr(GError) error = NULL;

	fu_context_add_backend(ctx, backend);
	backend2 = fu_context_get_backend_by_name(ctx, "dummy", &error);
	g_assert_no_error(error);
	g_assert_nonnull(backend2);
}

static void
fu_context_flags_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();

	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	g_assert_true(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
	fu_context_remove_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	fu_context_remove_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	g_assert_false(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS);
	g_assert_true(fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS));
}

static void
fu_context_state_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();

	g_assert_cmpint(fu_context_get_power_state(ctx), ==, FU_POWER_STATE_UNKNOWN);
	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_UNKNOWN);
	g_assert_cmpint(fu_context_get_display_state(ctx), ==, FU_DISPLAY_STATE_UNKNOWN);
	g_assert_cmpint(fu_context_get_battery_level(ctx), ==, FWUPD_BATTERY_LEVEL_INVALID);

	fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY_DISCHARGING);
	fu_context_set_power_state(ctx, FU_POWER_STATE_BATTERY_DISCHARGING);
	fu_context_set_lid_state(ctx, FU_LID_STATE_CLOSED);
	fu_context_set_lid_state(ctx, FU_LID_STATE_CLOSED);
	fu_context_set_display_state(ctx, FU_DISPLAY_STATE_CONNECTED);
	fu_context_set_display_state(ctx, FU_DISPLAY_STATE_CONNECTED);
	fu_context_set_battery_level(ctx, 50);
	fu_context_set_battery_level(ctx, 50);

	g_assert_cmpint(fu_context_get_power_state(ctx), ==, FU_POWER_STATE_BATTERY_DISCHARGING);
	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_CLOSED);
	g_assert_cmpint(fu_context_get_display_state(ctx), ==, FU_DISPLAY_STATE_CONNECTED);
	g_assert_cmpint(fu_context_get_battery_level(ctx), ==, 50);
}

static void
fu_context_firmware_gtypes_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GArray) gtypes = NULL;
	g_autoptr(GPtrArray) gtype_ids = NULL;

	fu_context_add_firmware_gtype(ctx, "base", FU_TYPE_FIRMWARE);

	gtype_ids = fu_context_get_firmware_gtype_ids(ctx);
	g_assert_nonnull(gtype_ids);
	g_assert_cmpint(gtype_ids->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(gtype_ids, 0), ==, "base");

	gtypes = fu_context_get_firmware_gtypes(ctx);
	g_assert_nonnull(gtypes);
	g_assert_cmpint(gtypes->len, ==, 1);
	g_assert_cmpint(g_array_index(gtypes, GType, 0), ==, FU_TYPE_FIRMWARE);

	g_assert_cmpint(fu_context_get_firmware_gtype_by_id(ctx, "base"), ==, FU_TYPE_FIRMWARE);
	g_assert_cmpint(fu_context_get_firmware_gtype_by_id(ctx, "n/a"), ==, G_TYPE_INVALID);
}

static void
fu_context_hwids_dmi_func(void)
{
	g_autofree gchar *dump = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_DMI, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(fu_context_get_smbios(ctx)));
	g_debug("%s", dump);

	g_assert_cmpstr(fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER), ==, "FwupdTest");
	g_assert_cmpuint(fu_context_get_chassis_kind(ctx), ==, 16);
}

static gboolean
fu_test_strnsplit_add_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	GPtrArray *array = (GPtrArray *)user_data;
	g_debug("TOKEN: [%s] (%u)", token->str, token_idx);
	g_ptr_array_add(array, g_strdup(token->str));
	return TRUE;
}

static gboolean
fu_test_strnsplit_nop_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
	return TRUE;
}

static void
fu_common_memmem_func(void)
{
	const guint8 haystack[] = {'H', 'A', 'Y', 'S'};
	const guint8 needle[] = {'A', 'Y'};
	gboolean ret;
	gsize offset = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_memmem_safe(haystack, sizeof(haystack), needle, sizeof(needle), &offset, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(offset, ==, 0x1);

	ret = fu_memmem_safe(haystack + 2,
			     sizeof(haystack) - 2,
			     needle,
			     sizeof(needle),
			     &offset,
			     &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_strpassmask_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"foo https://test.com/auth bar", "foo https://test.com/auth bar"},
		    {"foo https://user%40host:SECRET@test.com/auth bar",
		     "foo https://user%40host:XXXXXX@test.com/auth bar"},
		    {"foo https://user1%40host:SECRET@test.com/auth "
		     "https://user2%40host:SECRET2@test.com/auth bar",
		     "foo https://user1%40host:XXXXXX@test.com/auth "
		     "https://user2%40host:XXXXXXX@test.com/auth bar"},
		    {NULL, NULL}};
	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_strpassmask(strs[i].in);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_strsplit_func(void)
{
	const gchar *str = "123foo123bar123";
	const guint bigsz = 1024 * 1024;
	gboolean ret;
	guint cnt = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) bigstr = g_string_sized_new(bigsz * 2);

	/* works for me */
	ret = fu_strsplit_full(str, -1, "123", fu_test_strnsplit_add_cb, array, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(array->len, ==, 4);
	g_assert_cmpstr(g_ptr_array_index(array, 0), ==, "");
	g_assert_cmpstr(g_ptr_array_index(array, 1), ==, "foo");
	g_assert_cmpstr(g_ptr_array_index(array, 2), ==, "bar");
	g_assert_cmpstr(g_ptr_array_index(array, 3), ==, "");

	/* lets try something insane */
	for (guint i = 0; i < bigsz; i++)
		g_string_append(bigstr, "X\n");
	ret = fu_strsplit_full(bigstr->str, -1, "\n", fu_test_strnsplit_nop_cb, &cnt, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	/* we have an empty last section */
	g_assert_cmpint(cnt, ==, bigsz + 1);
}

static void
fu_common_olson_timezone_id_func(void)
{
	g_autofree gchar *timezone_id = NULL;
	g_autoptr(GError) error = NULL;

#ifdef HOST_MACHINE_SYSTEM_DARWIN
	g_test_skip("not supported on Darwin");
	return;
#endif

	timezone_id = fu_common_get_olson_timezone_id(&error);
	g_assert_no_error(error);
#ifdef _WIN32
	/* we do not emulate this on Windows, so just check for anything */
	g_assert_nonnull(timezone_id);
#else
	g_assert_cmpstr(timezone_id, ==, "America/New_York");
#endif
}

static void
fu_strsafe_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"dave123", "dave123"},
		    {"dave123XXX", "dave123"},
		    {"dave\x03XXX", "dave.XX"},
		    {"dave\x03\x04XXX", "dave..X"},
		    {"\x03\x03", NULL},
		    {NULL, NULL}};
	GPtrArray *instance_ids;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* check bespoke legacy instance ID behavior */
	fu_device_add_instance_strsafe(dev, "KEY", "_ _LEN&VO&\\&");
	ret = fu_device_build_instance_id(dev, &error, "SUB", "KEY", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	instance_ids = fu_device_get_instance_ids(dev);
	g_assert_cmpint(instance_ids->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(instance_ids, 0), ==, "SUB\\KEY_LEN-VO");

	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_strsafe(strs[i].in, 7);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_hwids_func(void)
{
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) context = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	struct {
		const gchar *key;
		const gchar *value;
	} guids[] = {{"Manufacturer", "6de5d951-d755-576b-bd09-c5cf66b27234"},
		     {"HardwareID-14", "6de5d951-d755-576b-bd09-c5cf66b27234"},
		     {"HardwareID-13", "f8e1de5f-b68c-5f52-9d1a-f1ba52f1f773"},
		     {"HardwareID-12", "e093d715-70f7-51f4-b6c8-b4a7e31def85"},
		     {"HardwareID-11", "db73af4c-4612-50f7-b8a7-787cf4871847"},
		     {"HardwareID-10", "f4275c1f-6130-5191-845c-3426247eb6a1"},
		     {"HardwareID-09", "0cf8618d-9eff-537c-9f35-46861406eb9c"},
		     {"HardwareID-08", "059eb22d-6dc7-59af-abd3-94bbe017f67c"},
		     {"HardwareID-07", "da1da9b6-62f5-5f22-8aaa-14db7eeda2a4"},
		     {"HardwareID-06", "178cd22d-ad9f-562d-ae0a-34009822cdbe"},
		     {"HardwareID-05", "8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814"},
		     {"HardwareID-04", "660ccba8-1b78-5a33-80e6-9fb8354ee873"},
		     {"HardwareID-03", "3faec92a-3ae3-5744-be88-495e90a7d541"},
		     {"HardwareID-02", "f5ff077f-3eeb-5bae-be1c-e98ffe8ce5f8"},
		     {"HardwareID-01", "b7cceb67-774c-537e-bf8b-22c6107e9a74"},
		     {"HardwareID-00", "147efce9-f201-5fc8-ab0c-c859751c3440"},
		     {NULL, NULL}};

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* these tests will not write */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);

	context = fu_context_new();
	ret = fu_context_load_hwinfo(context, progress, FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_MANUFACTURER),
			==,
			"LENOVO");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_ENCLOSURE_KIND), ==, "a");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_FAMILY),
			==,
			"ThinkPad T440s");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_PRODUCT_NAME),
			==,
			"20ARS19C0C");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_VENDOR), ==, "LENOVO");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_VERSION),
			==,
			"GJET75WW (2.25 )");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE),
			==,
			"02");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_BIOS_MINOR_RELEASE),
			==,
			"19");
	g_assert_cmpstr(fu_context_get_hwid_value(context, FU_HWIDS_KEY_PRODUCT_SKU),
			==,
			"LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s");
	for (guint i = 0; guids[i].key != NULL; i++) {
		FuHwids *hwids = fu_context_get_hwids(context);
		g_autofree gchar *guid = fu_hwids_get_guid(hwids, guids[i].key, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(guid, ==, guids[i].value);
	}
	for (guint i = 0; guids[i].key != NULL; i++)
		g_assert_true(fu_context_has_hwid_guid(context, guids[i].value));
}

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
}

static void
fu_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	g_autofree gchar *composite_data = NULL;
	g_autoptr(FuConfig) config = fu_config_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn_imu = NULL;
	g_autofree gchar *fn_mut = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* immutable file */
	(void)g_setenv("FWUPD_SYSCONFDIR", "/tmp/fwupd-self-test/etc/fwupd", TRUE);
	fn_imu = g_build_filename(g_getenv("FWUPD_SYSCONFDIR"), "fwupd.conf", NULL);
	g_assert_nonnull(fn_imu);
	ret = fu_path_mkdir_parent(fn_imu, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_imu);
	ret = g_file_set_contents(fn_imu,
				  "[fwupd]\n"
				  "Key=true\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_chmod(fn_imu, 0640);
	ret = g_stat(fn_imu, &statbuf);
	g_assert_cmpint(ret, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);

	/* mutable file */
	(void)g_setenv("LOCALCONF_DIRECTORY", "/tmp/fwupd-self-test/var/etc/fwupd", TRUE);
	fn_mut = g_build_filename(g_getenv("LOCALCONF_DIRECTORY"), "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_mut);
	ret = g_file_set_contents(fn_mut,
				  "# group comment\n"
				  "[fwupd]\n"
				  "# key comment\n"
				  "Key=false\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_load(config, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_set_value(config, "fwupd", "Key", "false", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = g_file_get_contents(fn_mut, &composite_data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_strstr_len(composite_data, -1, "Key=false") != NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "Key=true") == NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "# group comment") != NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "# key comment") != NULL);
	g_remove(fn_mut);
}

static void
fu_plugin_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	gint rc;
	g_autofree gchar *conf_dir = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *value = NULL;
	g_autofree gchar *value_missing = NULL;
	g_autofree gchar *fn_mut = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* remove existing file */
	(void)g_setenv("FWUPD_SYSCONFDIR", "/tmp/fwupd-self-test/etc/fwupd", TRUE);
	conf_dir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
	fu_plugin_set_name(plugin, "test");
	fn = g_build_filename(conf_dir, "fwupd.conf", NULL);
	ret = fu_path_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn);
	ret = g_file_set_contents(fn, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* mutable file we'll be writing */
	(void)g_setenv("LOCALCONF_DIRECTORY", "/tmp/fwupd-self-test/var/etc/fwupd", TRUE);
	fn_mut = g_build_filename(g_getenv("LOCALCONF_DIRECTORY"), "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_mut);
	ret = g_file_set_contents(fn_mut, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load context */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set a value */
	ret = fu_plugin_set_config_value(plugin, "Key", "True", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_file_test(fn, G_FILE_TEST_EXISTS));

	/* check it is only readable by the user/group */
	rc = g_stat(fn_mut, &statbuf);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);

	/* read back the value */
	fu_plugin_set_config_default(plugin, "NotGoingToExist", "Foo");
	value_missing = fu_plugin_get_config_value(plugin, "NotGoingToExist");
	g_assert_cmpstr(value_missing, ==, "Foo");
	value = fu_plugin_get_config_value(plugin, "Key");
	g_assert_cmpstr(value, ==, "True");
	g_assert_true(fu_plugin_get_config_value_boolean(plugin, "Key"));
}

static void
fu_plugin_devices_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	GPtrArray *devices;

	devices = fu_plugin_get_devices(plugin);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 0);

	fu_device_set_id(device, "testdev");
	fu_device_set_name(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_cmpint(devices->len, ==, 1);
	fu_plugin_device_remove(plugin, device);
	g_assert_cmpint(devices->len, ==, 0);

	/* add a child after adding the parent to the plugin */
	fu_device_set_id(child, "child");
	fu_device_set_name(child, "child");
	fu_device_add_child(device, child);
	g_assert_cmpint(devices->len, ==, 1);

	/* remove said child */
	fu_device_remove_child(device, child);
	g_assert_cmpint(devices->len, ==, 0);
}

static void
fu_plugin_device_inhibit_children_func(void)
{
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) child1 = fu_device_new(NULL);
	g_autoptr(FuDevice) child2 = fu_device_new(NULL);

	fu_device_set_id(parent, "testdev");
	fu_device_set_name(parent, "testdev");
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_id(child1, "child1");
	fu_device_set_name(child1, "child1");
	fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_child(parent, child1);

	/* inhibit the parent */
	fu_device_inhibit(parent, "test", "because");
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));
	fu_device_uninhibit(parent, "test");

	/* make the inhibit propagate to children */
	fu_device_add_private_flag(parent, FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	fu_device_inhibit(parent, "test", "because");
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* add a child after the inhibit, which should also be inhibited too */
	fu_device_set_id(child2, "child2");
	fu_device_set_name(child2, "child2");
	fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_child(parent, child2);
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_plugin_delay_func(void)
{
	FuDevice *device_tmp;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;

	plugin = fu_plugin_new(NULL);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device_tmp);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-removed",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device_tmp);

	/* add device straight away */
	device = fu_device_new(NULL);
	fu_device_set_id(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);

	/* remove device */
	fu_plugin_device_remove(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);
}

static void
fu_plugin_fdt_func(void)
{
	gboolean ret;
	g_autofree gchar *compatible = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuFirmware) fdt_root = NULL;
	g_autoptr(FuFirmware) fdt_tmp = fu_fdt_firmware_new();
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(FuFirmware) img3 = NULL;
	g_autoptr(FuFirmware) img4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file =
	    g_file_new_for_path("/tmp/fwupd-self-test/var/lib/fwupd/system.dtb");

	/* write file */
	ret = fu_firmware_build_from_xml(
	    FU_FIRMWARE(fdt_tmp),
	    "<firmware gtype=\"FuFdtFirmware\">\n"
	    "  <firmware gtype=\"FuFdtImage\">\n"
	    "    <metadata key=\"compatible\" format=\"str\">pine64,rockpro64-v2.1</metadata>\n"
	    "  </firmware>\n"
	    "</firmware>\n",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_write_file(FU_FIRMWARE(fdt_tmp), file, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get compatible from the context */
	fdt = fu_context_get_fdt(ctx, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt);
	fdt_root = fu_firmware_get_image_by_id(fdt, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fdt_root);
	ret = fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_root), "compatible", &compatible, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(compatible, ==, "pine64,rockpro64-v2.1");

	/* get by GType */
	img2 = fu_firmware_get_image_by_gtype(fdt, FU_TYPE_FIRMWARE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	img3 = fu_firmware_get_image_by_gtype(fdt, FU_TYPE_FDT_IMAGE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img3);
	img4 = fu_firmware_get_image_by_gtype(fdt, G_TYPE_STRING, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img4);
}

static void
fu_plugin_quirks_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* USB\\VID_0A5C&PID_6412 */
	tmp = fu_context_lookup_quirk_by_id(ctx, "7a1ba7b9-6bcd-54a4-8a36-d60cc5ee935c", "Flags");
	g_assert_cmpstr(tmp, ==, "ignore-runtime");

	/* ACME Inc.=True */
	tmp = fu_context_lookup_quirk_by_id(ctx, "ec77e295-7c63-5935-9957-be0472d9593a", "Name");
	g_assert_cmpstr(tmp, ==, "awesome");

	/* CORP* */
	tmp = fu_context_lookup_quirk_by_id(ctx, "3731cce4-484c-521f-a652-892c8e0a65c7", "Name");
	g_assert_cmpstr(tmp, ==, "town");

	/* baz */
	tmp = fu_context_lookup_quirk_by_id(ctx, "579a3b1c-d1db-5bdc-b6b9-e2c1b28d5b8a", "Unfound");
	g_assert_cmpstr(tmp, ==, NULL);

	/* unfound */
	tmp = fu_context_lookup_quirk_by_id(ctx, "8ff2ed23-b37e-5f61-b409-b7fe9563be36", "tests");
	g_assert_cmpstr(tmp, ==, NULL);

	/* unfound */
	tmp = fu_context_lookup_quirk_by_id(ctx, "8ff2ed23-b37e-5f61-b409-b7fe9563be36", "unfound");
	g_assert_cmpstr(tmp, ==, NULL);

	/* GUID */
	tmp = fu_context_lookup_quirk_by_id(ctx, "bb9ec3e2-77b3-53bc-a1f1-b05916715627", "Flags");
	g_assert_cmpstr(tmp, ==, "clever");
}

static void
fu_plugin_quirks_performance_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(GError) error = NULL;
	const gchar *keys[] = {"Name", "Children", "Flags", NULL};

	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* lookup */
	g_timer_reset(timer);
	for (guint j = 0; j < 1000; j++) {
		const gchar *group = "bb9ec3e2-77b3-53bc-a1f1-b05916715627";
		for (guint i = 0; keys[i] != NULL; i++) {
			const gchar *tmp = fu_quirks_lookup_by_id(quirks, group, keys[i]);
			g_assert_cmpstr(tmp, !=, NULL);
		}
	}
	g_print("lookup=%.3fms ", g_timer_elapsed(timer, NULL) * 1000.f);
}

typedef struct {
	gboolean seen_one;
	gboolean seen_two;
} FuPluginQuirksAppendHelper;

static void
fu_plugin_quirks_append_cb(FuQuirks *quirks,
			   const gchar *key,
			   const gchar *value,
			   gpointer user_data)
{
	FuPluginQuirksAppendHelper *helper = (FuPluginQuirksAppendHelper *)user_data;
	g_debug("key=%s, value=%s", key, value);
	if (g_strcmp0(key, "Plugin") == 0 && g_strcmp0(value, "one") == 0) {
		helper->seen_one = TRUE;
		return;
	}
	if (g_strcmp0(key, "Plugin") == 0 && g_strcmp0(value, "two") == 0) {
		helper->seen_two = TRUE;
		return;
	}
	g_assert_not_reached();
}

static void
fu_plugin_device_progress_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDeviceProgress) device_progress = fu_device_progress_new(device, progress);

	/* proxy */
	fu_progress_set_percentage(progress, 50);
	fu_progress_set_status(progress, FWUPD_STATUS_SHUTDOWN);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 50);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_SHUTDOWN);

	/* clear */
	g_clear_object(&device_progress);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 0);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_IDLE);

	/* do not proxy */
	fu_progress_set_percentage(progress, 100);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 0);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_IDLE);
}

static void
fu_plugin_quirks_append_func(void)
{
	FuPluginQuirksAppendHelper helper = {0};
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

	/* lookup a duplicate group name */
	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_quirks_lookup_by_id_iter(quirks,
					  "b19d1c67-a29a-51ce-9cae-f7b40fe5505b",
					  NULL,
					  fu_plugin_quirks_append_cb,
					  &helper);
	g_assert_true(ret);
	g_assert_true(helper.seen_one);
	g_assert_true(helper.seen_two);
}

static void
fu_quirks_vendor_ids_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autofree gchar *guid1 = fwupd_guid_hash_string("PCI\\VEN_8086");
	g_autofree gchar *guid2 = fwupd_guid_hash_string("USB\\VID_8086");
	g_autofree gchar *guid3 = fwupd_guid_hash_string("PNP\\VID_ICO");
	g_autofree gchar *datadata = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
	g_autofree gchar *quirksdb = g_build_filename(datadata, "quirks.db", NULL);
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

#ifndef HAVE_SQLITE
	g_test_skip("no sqlite");
	return;
#endif
	g_debug("deleting %s if exists", quirksdb);
	g_unlink(quirksdb);

	/* lookup a duplicate group name */
	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_quirks_lookup_by_id(quirks, guid1, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corporation");
	tmp = fu_quirks_lookup_by_id(quirks, guid2, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corp.");
	tmp = fu_quirks_lookup_by_id(quirks, guid3, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corp");
}

static void
fu_plugin_func(void)
{
	GHashTable *metadata;
	GPtrArray *rules;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);

	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "dave1");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "dave2");
	rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_CONFLICTS);
	g_assert_nonnull(rules);
	g_assert_cmpint(rules->len, ==, 2);
	rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_RUN_AFTER);
	g_assert_null(rules);

	fu_plugin_add_report_metadata(plugin, "key", "value");
	metadata = fu_plugin_get_report_metadata(plugin);
	g_assert_nonnull(metadata);
	g_assert_cmpint(g_hash_table_size(metadata), ==, 1);
}

static void
fu_plugin_vfuncs_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(GError) error = NULL;

	/* nop: error */
	ret = fu_plugin_runner_modify_config(plugin, "foo", "bar", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_plugin_device_gtype_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);

	/* add the same gtype multiple times */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, FU_TYPE_DEVICE);

	/* now there's no explicit default */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TEST_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, G_TYPE_INVALID);

	/* make it explicit */
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_TEST_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, FU_TYPE_TEST_DEVICE);
}

static void
fu_plugin_backend_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRFUNC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_backend_device_changed(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_specialized_gtype(device, FU_TYPE_DEVICE);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ONLY_SUPPORTED);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_plugin_backend_proxy_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
}

static void
fu_plugin_backend_proxy_device_func(void)
{
	gboolean ret;
	FuDevice *proxy;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) device_new = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRFUNC);
	g_autoptr(GError) error = NULL;

	fu_device_set_id(device, "testdev");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	ret = fu_plugin_runner_backend_device_changed(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* watch for the new superclassed device */
	g_signal_connect(plugin,
			 "device-added",
			 G_CALLBACK(fu_test_plugin_backend_proxy_device_added_cb),
			 &device_new);

	fu_device_set_specialized_gtype(device, FU_TYPE_DEVICE);
	fu_device_set_proxy_gtype(device, FU_TYPE_TEST_DEVICE);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check device was constructed */
	g_assert_nonnull(device_new);
	g_assert_true(FU_IS_DEVICE(device_new));

	/* check proxy was constructed */
	proxy = fu_device_get_proxy(device_new);
	g_assert_nonnull(proxy);
	g_assert_true(FU_IS_TEST_DEVICE(proxy));
}

static void
fu_plugin_quirks_device_func(void)
{
	FuDevice *device_tmp;
	GPtrArray *children;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* use quirk file to set device attributes */
	fu_device_set_physical_id(device, "usb:00:05");
	fu_device_set_context(device, ctx);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id_full(device,
				       "USB\\VID_0BDA&PID_1100",
				       FU_DEVICE_INSTANCE_FLAG_GENERIC |
					   FU_DEVICE_INSTANCE_FLAG_QUIRKS |
					   FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_add_instance_id(device, "USB\\VID_0BDA&PID_1100&CID_1234");
	fu_device_convert_instance_ids(device);
	g_assert_cmpstr(fu_device_get_name(device), ==, "Hub");

	/* ensure the non-customer-id instance ID is not available */
	g_assert_true(fu_device_has_instance_id(device, "USB\\VID_0BDA&PID_1100&CID_1234"));
	g_assert_false(fu_device_has_instance_id(device, "USB\\VID_0BDA&PID_1100"));

	/* ensure children are created */
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, ==, 1);
	device_tmp = g_ptr_array_index(children, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "HDMI");
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_common_kernel_lockdown_func(void)
{
	gboolean ret;
	g_autofree gchar *locked_dir = NULL;
	g_autofree gchar *none_dir = NULL;
	g_autofree gchar *old_kernel_dir = NULL;

#ifndef __linux__
	g_test_skip("only works on Linux");
	return;
#endif

	old_kernel_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", old_kernel_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_false(ret);

	locked_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", "locked", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", locked_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_true(ret);

	none_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", "none", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", none_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_false(ret);
}

static gboolean
fu_test_open_cb(GObject *device, GError **error)
{
	g_assert_cmpstr(g_object_get_data(device, "state"), ==, "closed");
	g_object_set_data(device, "state", (gpointer) "opened");
	return TRUE;
}

static gboolean
fu_test_close_cb(GObject *device, GError **error)
{
	g_assert_cmpstr(g_object_get_data(device, "state"), ==, "opened");
	g_object_set_data(device, "state", (gpointer) "closed-on-unref");
	return TRUE;
}

static void
fu_device_locker_func(void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new(G_TYPE_OBJECT, NULL);

	g_object_set_data(device, "state", (gpointer) "closed");
	locker = fu_device_locker_new_full(device, fu_test_open_cb, fu_test_close_cb, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	g_clear_object(&locker);
	g_assert_cmpstr(g_object_get_data(device, "state"), ==, "closed-on-unref");
}

static gboolean
fu_test_fail_open_cb(FuDevice *device, GError **error)
{
	fu_device_set_metadata_boolean(device, "Test::Open", TRUE);
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "fail");
	return FALSE;
}

static gboolean
fu_test_fail_close_cb(FuDevice *device, GError **error)
{
	fu_device_set_metadata_boolean(device, "Test::Close", TRUE);
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "busy");
	return FALSE;
}

static void
fu_device_locker_fail_func(void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_test_fail_open_cb,
					   (FuDeviceLockerFunc)fu_test_fail_close_cb,
					   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_null(locker);
	g_assert_true(fu_device_get_metadata_boolean(device, "Test::Open"));
	g_assert_true(fu_device_get_metadata_boolean(device, "Test::Close"));
	g_assert_false(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_IS_OPEN));
}

static void
fu_common_endian_func(void)
{
	guint8 buf[3];

	fu_memwrite_uint16(buf, 0x1234, G_LITTLE_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x34);
	g_assert_cmpint(buf[1], ==, 0x12);
	g_assert_cmpint(fu_memread_uint16(buf, G_LITTLE_ENDIAN), ==, 0x1234);

	fu_memwrite_uint16(buf, 0x1234, G_BIG_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(fu_memread_uint16(buf, G_BIG_ENDIAN), ==, 0x1234);

	fu_memwrite_uint24(buf, 0x123456, G_LITTLE_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x56);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(buf[2], ==, 0x12);
	g_assert_cmpint(fu_memread_uint24(buf, G_LITTLE_ENDIAN), ==, 0x123456);

	fu_memwrite_uint24(buf, 0x123456, G_BIG_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(buf[2], ==, 0x56);
	g_assert_cmpint(fu_memread_uint24(buf, G_BIG_ENDIAN), ==, 0x123456);
}

static void
fu_common_bytes_get_data_func(void)
{
	const gchar *fn = "/tmp/fwupdzero";
	const guint8 *buf;
	gboolean ret;
	g_autoptr(GBytes) bytes1 = NULL;
	g_autoptr(GBytes) bytes2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GMappedFile) mmap = NULL;

	/* create file with zero size */
	ret = g_file_set_contents(fn, NULL, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we got zero sized data */
	bytes1 = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes1);
	g_assert_cmpint(g_bytes_get_size(bytes1), ==, 0);
	g_assert_nonnull(g_bytes_get_data(bytes1, NULL));

	/* do the same with an mmap mapping, which returns NULL on empty file */
	mmap = g_mapped_file_new(fn, FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(mmap);
	bytes2 = g_mapped_file_get_bytes(mmap);
	g_assert_nonnull(bytes2);
	g_assert_cmpint(g_bytes_get_size(bytes2), ==, 0);
	g_assert_null(g_bytes_get_data(bytes2, NULL));

	/* use the safe function */
	buf = fu_bytes_get_data_safe(bytes2, NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(buf);
}

static gboolean
fu_device_poll_cb(FuDevice *device, GError **error)
{
	guint64 cnt = fu_device_get_metadata_integer(device, "cnt");
	g_debug("poll cnt=%" G_GUINT64_FORMAT, cnt);
	fu_device_set_metadata_integer(device, "cnt", cnt + 1);
	return TRUE;
}

static void
fu_device_poll_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(device);
	guint cnt;

	klass->poll = fu_device_poll_cb;
	fu_device_set_metadata_integer(device, "cnt", 0);

	/* manual poll */
	ret = fu_device_poll(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, ==, 1);

	/* set up a 10ms poll */
	fu_device_set_poll_interval(device, 5);
	fu_test_loop_run_with_timeout(50);
	fu_test_loop_quit();
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, >=, 5);
	fu_test_loop_quit();

	/* auto pause */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING);
	locker = fu_device_poll_locker_new(device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	fu_test_loop_run_with_timeout(25);
	g_clear_object(&locker);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
	fu_test_loop_quit();

	/* disable the poll manually */
	fu_device_set_poll_interval(device, 0);
	fu_test_loop_run_with_timeout(25);
	fu_test_loop_quit();
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
	fu_test_loop_quit();
}

static void
fu_device_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GPtrArray) possible_plugins = NULL;

	/* only add one plugin name of the same type */
	fu_device_add_possible_plugin(device, "test");
	fu_device_add_possible_plugin(device, "test");
	possible_plugins = fu_device_get_possible_plugins(device);
	g_assert_cmpint(possible_plugins->len, ==, 1);
}

static void
fu_device_event_donor_func(void)
{
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);
	g_autoptr(FuDeviceEvent) event1 = fu_device_event_new("foo:bar:baz");
	g_autoptr(FuDeviceEvent) event2 = fu_device_event_new("aaa:bbb:ccc");
	g_autoptr(FuDeviceEvent) event3 = fu_device_event_new("foo:111:222");
	GPtrArray *events;

	fu_device_add_event(device1, event1);
	fu_device_add_event(device2, event2);
	fu_device_set_target(device1, device2);

	/* did we incorporate */
	events = fu_device_get_events(device2);
	g_assert_nonnull(events);
	g_assert_cmpint(events->len, ==, 2);

	/* make sure it is redirected */
	fu_device_add_event(device1, event3);
	events = fu_device_get_events(device2);
	g_assert_nonnull(events);
	g_assert_cmpint(events->len, ==, 3);
}

static void
fu_device_event_func(void)
{
	gboolean ret;
	const gchar *str;
	g_autofree gchar *json = NULL;
	g_autoptr(FuDeviceEvent) event1 = fu_device_event_new("foo:bar:baz");
	g_autoptr(FuDeviceEvent) event2 = fu_device_event_new(NULL);
	g_autoptr(GBytes) blob1 = g_bytes_new_static("hello", 6);
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(GError) error = NULL;

	fu_device_event_set_str(event1, "Name", "Richard");
	fu_device_event_set_i64(event1, "Age", 123);
	fu_device_event_set_bytes(event1, "Blob", blob1);
	fu_device_event_set_data(event1, "Data", NULL, 0);

	json = fwupd_codec_to_json_string(FWUPD_CODEC(event1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(json,
			==,
			"{\n"
			"  \"Id\" : \"foo:bar:baz\",\n"
			"  \"Data\" : \"\",\n"
			"  \"Age\" : 123,\n"
			"  \"Name\" : \"Richard\",\n"
			"  \"Blob\" : \"aGVsbG8A\"\n"
			"}");

	ret = fwupd_codec_from_json_string(FWUPD_CODEC(event2), json, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_event_get_id(event2), ==, "foo:bar:baz");
	g_assert_cmpint(fu_device_event_get_i64(event2, "Age", NULL), ==, 123);
	g_assert_cmpstr(fu_device_event_get_str(event2, "Name", NULL), ==, "Richard");
	blob2 = fu_device_event_get_bytes(event2, "Blob", &error);
	g_assert_nonnull(blob2);
	g_assert_cmpstr(g_bytes_get_data(blob2, NULL), ==, "hello");
	blob3 = fu_device_event_get_bytes(event2, "Data", &error);
	g_assert_nonnull(blob3);
	g_assert_cmpstr(g_bytes_get_data(blob3, NULL), ==, NULL);

	/* invalid type */
	str = fu_device_event_get_str(event2, "Age", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(str);
}

static void
fu_device_vfuncs_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GInputStream) istream = g_memory_input_stream_new();
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* nop: error */
	ret = fu_device_get_results(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	ret = fu_device_write_firmware(device, istream, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	firmware = fu_device_read_firmware(device, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(firmware);
	g_clear_error(&error);

	blob = fu_device_dump_firmware(device, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(blob);
	g_clear_error(&error);

	ret = fu_device_unbind_driver(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
	ret = fu_device_bind_driver(device, "subsystem", "driver", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* nop: ignore */
	ret = fu_device_detach(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_attach(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_activate(device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no-probe */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_NO_PROBE);
	ret = fu_device_probe(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_device_instance_ids_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* sanity check */
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* add a deferred instance ID that only gets converted on ->setup */
	fu_device_add_instance_id(device, "foobarbaz");
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	ret = fu_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* this gets added immediately */
	fu_device_add_instance_id(device, "bazbarfoo");
	g_assert_true(fu_device_has_guid(device, "77e49bb0-2cd6-5faf-bcee-5b7fbe6e944d"));
}

static void
fu_device_composite_id_func(void)
{
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev3 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev4 = fu_device_new(NULL);

	/* single device */
	fu_device_set_id(dev1, "dev1");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	fu_device_set_id(dev2, "dev2");

	/* one child */
	fu_device_add_child(dev1, dev2);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* add a different "family" */
	fu_device_set_id(dev3, "dev3");
	fu_device_set_id(dev4, "dev4");
	fu_device_add_child(dev3, dev4);
	fu_device_add_child(dev2, dev3);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev3),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev4),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* change the parent ID */
	fu_device_set_id(dev1, "dev1-NEW");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
}

static void
fu_device_inhibit_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_battery_threshold(device, 25);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));

	/* does not exist -> fine */
	fu_device_uninhibit(device, "NOTGOINGTOEXIST");
	g_assert_false(fu_device_has_inhibit(device, "NOTGOINGTOEXIST"));

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_true(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* another */
	fu_device_set_battery_level(device, 5);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* activated, power still too low */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* we got some more power -> fine */
	fu_device_set_battery_level(device, 95);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

static void
fu_device_inhibit_updateable_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, "Device is pending activation");

	/* activated, but still not updatable */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);
}

static void
fu_device_custom_flags_func(void)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_register_private_flag(device, "foo");
	fu_device_register_private_flag(device, "bar");

	fu_device_set_custom_flags(device, "foo");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	fu_device_set_custom_flags(device, "bar");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_true(fu_device_has_private_flag(device, "bar"));
	fu_device_set_custom_flags(device, "~bar");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_false(fu_device_has_private_flag(device, "bar"));
	fu_device_set_custom_flags(device, "baz");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_false(fu_device_has_private_flag(device, "bar"));

	tmp = fu_device_to_string(device);
	g_assert_cmpstr(tmp,
			==,
			"FuDevice:\n"
			"  Flags:                none\n"
			"  AcquiesceDelay:       50\n"
			"  CustomFlags:          baz\n"
			"  PrivateFlags:         foo\n");
}

static void
fu_device_flags_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) proxy = fu_device_new(NULL);

	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NONE);

	/* remove IS_BOOTLOADER if is a BOOTLOADER */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

	/* check implication */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE | FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_remove_flag(device,
			      FWUPD_DEVICE_FLAG_CAN_VERIFY | FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);

	/* negation */
	fu_device_set_custom_flags(device, "is-bootloader,updatable");
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_IS_BOOTLOADER | FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_custom_flags(device, "~is-bootloader");
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* setting flags on the proxy should propagate to the device that *uses* the proxy */
	fu_device_set_proxy(device, proxy);
	fu_device_add_flag(proxy, FWUPD_DEVICE_FLAG_EMULATED);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));
}

static void
fu_device_children_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up family */
	fu_device_add_child(parent, child);

	/* set an instance ID that will be converted to a GUID when the parent
	 * calls ->setup */
	fu_device_add_instance_id(child, "foo");
	g_assert_false(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));

	/* setup parent, which also calls setup on child too (and thus also
	 * converts the instance ID to a GUID) */
	ret = fu_device_setup(parent, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));
}

static void
fu_device_parent_func(void)
{
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) child_root = NULL;
	g_autoptr(FuDevice) grandparent = fu_device_new(NULL);
	g_autoptr(FuDevice) grandparent_root = NULL;
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) parent_root = NULL;

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(grandparent, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up three layer family */
	fu_device_add_child(grandparent, parent);
	fu_device_add_child(parent, child);

	/* check parents */
	g_assert_true(fu_device_get_parent(child) == parent);
	g_assert_true(fu_device_get_parent(parent) == grandparent);
	g_assert_true(fu_device_get_parent(grandparent) == NULL);

	/* check root */
	child_root = fu_device_get_root(child);
	g_assert_true(child_root == grandparent);
	parent_root = fu_device_get_root(parent);
	g_assert_true(parent_root == grandparent);
	grandparent_root = fu_device_get_root(child);
	g_assert_true(grandparent_root == grandparent);
}

static void
fu_device_incorporate_descendant_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuTestDevice) test_device = g_object_new(FU_TYPE_TEST_DEVICE, NULL);

	fu_device_set_name(device, "FuDevice");
	fu_device_set_summary(FU_DEVICE(test_device), "FuTestDevice");

	fu_device_incorporate(FU_DEVICE(test_device), device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(test_device)), ==, "FuDevice");

	/* this won't explode as device_class->incorporate is checking types */
	fu_device_incorporate(device, FU_DEVICE(test_device), FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_summary(device), ==, "FuTestDevice");
}

static void
fu_device_incorporate_flag_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);

	fu_device_set_logical_id(donor, "logi");
	fu_device_set_physical_id(donor, "phys");
	fu_device_add_vendor_id(donor, "PCI:0x1234");

	fu_device_incorporate(device,
			      donor,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "phys");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
	g_assert_true(fu_device_has_vendor_id(device, "PCI:0x1234"));

	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, "logi");
}

static void
fu_device_incorporate_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* load quirks */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set up donor device */
	fu_device_set_equivalent_id(donor, "0000000000000000000000000000000000000000");
	fu_device_set_metadata(donor, "test", "me");
	fu_device_set_metadata(donor, "test2", "me");
	fu_device_add_instance_str(donor, "VID", "0A5C");
	fu_device_add_instance_u16(donor, "PID", 0x6412);
	fu_device_add_instance_u32(donor, "BOARD_ID", 0x12345678);

	/* match a quirk entry, and then clear to ensure encorporate uses the quirk instance ID */
	ret = fu_device_build_instance_id_full(donor,
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					       &error,
					       "USB",
					       "VID",
					       "PID",
					       NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_custom_flags(donor), ==, "ignore-runtime");
	fu_device_set_custom_flags(donor, "SHOULD_BE_REPLACED_WITH_QUIRK_VALUE");

	/* base properties */
	fu_device_add_flag(donor, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_created_usec(donor, 1514338000ull * G_USEC_PER_SEC);
	fu_device_set_modified_usec(donor, 1514338999ull * G_USEC_PER_SEC);
	fu_device_add_icon(donor, "computer");

	/* existing properties */
	fu_device_set_equivalent_id(device, "ffffffffffffffffffffffffffffffffffffffff");
	fu_device_set_metadata(device, "test2", "DO_NOT_OVERWRITE");
	fu_device_set_modified_usec(device, 1514340000ull * G_USEC_PER_SEC);

	/* incorporate properties from donor to device */
	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_equivalent_id(device),
			==,
			"ffffffffffffffffffffffffffffffffffffffff");
	g_assert_cmpstr(fu_device_get_metadata(device, "test"), ==, "me");
	g_assert_cmpstr(fu_device_get_metadata(device, "test2"), ==, "DO_NOT_OVERWRITE");
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_cmpint(fu_device_get_created_usec(device), ==, 1514338000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_modified_usec(device), ==, 1514340000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_icons(device)->len, ==, 1);
	ret = fu_device_build_instance_id(device, &error, "USB", "VID", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_instance_id(device, "USB\\VID_0A5C"));
	g_assert_cmpstr(fu_device_get_custom_flags(device), ==, "ignore-runtime");
}

static void
fu_backend_emulate_count_cb(FuBackend *backend, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_backend_emulate_func(void)
{
	gboolean ret;
	guint8 buf[] = {0x00, 0x00};
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;
	FuDevice *device;
	g_autofree gchar *json3 = NULL;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	const gchar *json1 = "{"
			     "  \"UsbDevices\" : ["
			     "    {"
			     "      \"GType\" : \"FuUdevDevice\",\n"
			     "      \"BackendId\" : \"foo:bar:baz\","
			     "      \"Created\" : \"2023-02-01T16:35:03.302027Z\","
			     "      \"Events\" : ["
			     "        {"
			     "          \"Id\" : \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\","
			     "          \"Data\" : \"Aw==\","
			     "          \"DataOut\" : \"Aw==\""
			     "        },"
			     "        {"
			     "          \"Id\" : \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\","
			     "          \"Data\" : \"Aw==\","
			     "          \"DataOut\" : \"Aw==\""
			     "        }"
			     "      ]"
			     "    }"
			     "  ]"
			     "}";
	const gchar *json2 = "{\n"
			     "  \"UsbDevices\" : [\n"
			     "    {\n"
			     "      \"GType\" : \"FuUdevDevice\",\n"
#if GLIB_CHECK_VERSION(2, 80, 0)
			     "      \"BackendId\" : \"usb:FF:FF:06\",\n"
			     "      \"Created\" : \"2099-02-01T16:35:03Z\"\n"
#else
			     "      \"BackendId\" : \"usb:FF:FF:06\"\n"
#endif
			     "    }\n"
			     "  ]\n"
			     "}";

	/* watch events */
	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	g_signal_connect(FU_BACKEND(backend),
			 "device-added",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &added_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-removed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-changed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &changed_cnt);

	/* parse */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* get device */
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

#ifndef HAVE_IOCTL_H
	g_test_skip("no <ioctl.h> support");
	return;
#endif

	/* in-order */
	ret = fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
				   123,
				   buf,
				   sizeof(buf),
				   NULL,
				   0,
				   FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* in-order, repeat */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
				   123,
				   buf,
				   sizeof(buf),
				   NULL,
				   0,
				   FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* out-of-order */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
				   123,
				   buf,
				   sizeof(buf),
				   NULL,
				   0,
				   FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the same data */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

	/* load a different device */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(changed_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "usb:FF:FF:06");
	g_assert_no_error(error);
	g_assert_nonnull(device);

	/* save to string */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG);
	json3 = fwupd_codec_to_json_string(FWUPD_CODEC(backend), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json3);
	g_debug("%s", json3);
	g_assert_cmpstr(json3, ==, json2);
}

static void
fu_backend_func(void)
{
	FuDevice *dev;
	gboolean ret;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, NULL);
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* defaults */
	g_assert_null(fu_backend_get_name(backend));
	g_assert_true(fu_backend_get_enabled(backend));

	/* load */
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add two devices, then remove one of them */
	fu_device_set_physical_id(dev1, "dev1");
	fu_backend_device_added(backend, dev1);
	fu_device_set_physical_id(dev2, "dev2");
	fu_backend_device_added(backend, dev2);
	fu_backend_device_changed(backend, dev2);
	fu_backend_device_removed(backend, dev2);

	dev = fu_backend_lookup_by_id(backend, "dev1");
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);

	/* should have been removed */
	dev = fu_backend_lookup_by_id(backend, "dev2");
	g_assert_null(dev);

	/* get linear array */
	devices = fu_backend_get_devices(backend);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	dev = g_ptr_array_index(devices, 0);
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);
}

static void
fu_chunk_array_func(void)
{
	g_autoptr(FuChunk) chk1 = NULL;
	g_autoptr(FuChunk) chk2 = NULL;
	g_autoptr(FuChunk) chk3 = NULL;
	g_autoptr(FuChunk) chk4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) fw = g_bytes_new_static("hello world", 11);
	g_autoptr(FuChunkArray) chunks = fu_chunk_array_new_from_bytes(fw, 100, 5);

	g_assert_cmpint(fu_chunk_array_length(chunks), ==, 3);

	chk1 = fu_chunk_array_index(chunks, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chk1);
	g_assert_cmpint(fu_chunk_get_idx(chk1), ==, 0x0);
	g_assert_cmpint(fu_chunk_get_address(chk1), ==, 100);
	g_assert_cmpint(fu_chunk_get_data_sz(chk1), ==, 0x5);
	g_assert_cmpint(strncmp((const gchar *)fu_chunk_get_data(chk1), "hello", 5), ==, 0);

	chk2 = fu_chunk_array_index(chunks, 1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chk2);
	g_assert_cmpint(fu_chunk_get_idx(chk2), ==, 0x1);
	g_assert_cmpint(fu_chunk_get_address(chk2), ==, 105);
	g_assert_cmpint(fu_chunk_get_data_sz(chk2), ==, 0x5);
	g_assert_cmpint(strncmp((const gchar *)fu_chunk_get_data(chk2), " world", 6), ==, 0);

	chk3 = fu_chunk_array_index(chunks, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chk3);
	g_assert_cmpint(fu_chunk_get_idx(chk3), ==, 0x2);
	g_assert_cmpint(fu_chunk_get_address(chk3), ==, 110);
	g_assert_cmpint(fu_chunk_get_data_sz(chk3), ==, 0x1);
	g_assert_cmpint(strncmp((const gchar *)fu_chunk_get_data(chk3), "d", 1), ==, 0);

	chk4 = fu_chunk_array_index(chunks, 3, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(chk4);
	chk4 = fu_chunk_array_index(chunks, 1024, NULL);
	g_assert_null(chk4);
}

static void
fu_chunk_func(void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autofree gchar *chunked3_str = NULL;
	g_autofree gchar *chunked4_str = NULL;
	g_autofree gchar *chunked5_str = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;
	g_autoptr(GPtrArray) chunked5 = NULL;

	chunked3 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x0, 3, 3);
	chunked3_str = fu_chunk_array_to_string(chunked3);
	g_assert_cmpstr(chunked3_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x3\">123</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x3\">456</data>\n"
			"  </chunk>\n"
			"</chunks>\n");

	chunked4 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x4, 4, 4);
	chunked4_str = fu_chunk_array_to_string(chunked4);
	g_assert_cmpstr(chunked4_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">1234</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <page>0x2</page>\n"
			"    <data size=\"0x2\">56</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
	chunked5 = fu_chunk_array_new(NULL, 0, 0x0, 0x0, 4);
	g_assert_cmpint(chunked5->len, ==, 0);
	chunked5_str = fu_chunk_array_to_string(chunked5);
	g_assert_cmpstr(chunked5_str, ==, "<chunks>\n</chunks>\n");

	chunked1 = fu_chunk_array_new((const guint8 *)"0123456789abcdef", 16, 0x0, 10, 4);
	chunked1_str = fu_chunk_array_to_string(chunked1);
	g_assert_cmpstr(chunked1_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x4\">0123</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x4\">4567</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <addr>0x8</addr>\n"
			"    <data size=\"0x2\">89</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">abcd</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x4</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">ef</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
	chunked2 = fu_chunk_array_new((const guint8 *)"XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4);
	chunked2_str = fu_chunk_array_to_string(chunked2);
	g_print("\n%s", chunked2_str);
	g_assert_cmpstr(chunked2_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x4\">XXXX</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">XX</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">YYYY</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">YY</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x4</idx>\n"
			"    <page>0x2</page>\n"
			"    <data size=\"0x4\">ZZZZ</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x5</idx>\n"
			"    <page>0x2</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">ZZ</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
}

static void
fu_strstrip_func(void)
{
	struct {
		const gchar *old;
		const gchar *new;
	} map[] = {{"same", "same"},
		   {" leading", "leading"},
		   {"tailing ", "tailing"},
		   {"  b  ", "b"},
		   {"  ", ""},
		   {NULL, NULL}};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_strstrip(map[i].old);
		g_assert_cmpstr(tmp, ==, map[i].new);
	}
}

static void
fu_version_semver_func(void)
{
	struct {
		const gchar *old;
		const gchar *new;
		FwupdVersionFormat fmt;
	} map[] = {{"1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1.2.3.4", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1.2", "0.1.2", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1", "0.0.1", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"CBET1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"4.11-1190-g12d8072e6b-dirty", "4.11.1190", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"4.11-1190-g12d8072e6b-dirty", "4.11", FWUPD_VERSION_FORMAT_PAIR},
		   {NULL, NULL}};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_version_ensure_semver(map[i].old, map[i].fmt);
		g_assert_cmpstr(tmp, ==, map[i].new);
	}
}

static void
fu_strtoull_func(void)
{
	gboolean ret;
	guint64 val = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_strtoull("123", &val, 123, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoull("123\n", &val, 0, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoull("0x123", &val, 0, 0x123, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 0x123);

	ret = fu_strtoull(NULL, &val, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("124", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("119", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
}

static void
fu_strtoll_func(void)
{
	gboolean ret;
	gint64 val = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_strtoll("123", &val, 123, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoll("-123\n", &val, -123, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, -123);

	ret = fu_strtoll("0x123", &val, 0, 0x123, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 0x123);

	ret = fu_strtoll(NULL, &val, 0, G_MAXINT32, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoll("", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoll("124", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoll("-124", &val, -123, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
}

static void
fu_common_version_func(void)
{
	guint i;
	struct {
		guint32 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint32[] = {
	    {0x0, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.0.0.255", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff01, "0.0.255.1", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff0001, "0.255.0.1", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff000100, "255.0.1.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0x0, "0.0.0", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff, "0.0.255", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff01, "0.0.65281", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff0001, "0.255.1", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff000100, "255.0.256", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0xff000100, "4278190336", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x0, "11.0.0.0", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0xffffffff, "18.31.255.65535", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0x0b32057a, "11.11.50.1402", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0xb8320d84, "11.8.50.3460", FWUPD_VERSION_FORMAT_INTEL_ME2},
	    {0x226a4b00, "137.2706.768", FWUPD_VERSION_FORMAT_SURFACE_LEGACY},
	    {0x6001988, "6.25.136", FWUPD_VERSION_FORMAT_SURFACE},
	    {0x00ff0001, "255.0.1", FWUPD_VERSION_FORMAT_DELL_BIOS},
	    {0x010f0201, "1.15.2", FWUPD_VERSION_FORMAT_DELL_BIOS_MSB},
	    {0xc8, "0x000000c8", FWUPD_VERSION_FORMAT_HEX},
	};
	struct {
		guint32 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint24[] = {
	    {0x0, "0.0.0", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff, "0.0.255", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0xc8, "0x0000c8", FWUPD_VERSION_FORMAT_HEX},
	};
	struct {
		guint64 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint64[] = {
	    {0x0, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.0.0.255", FWUPD_VERSION_FORMAT_QUAD},
	    {0xffffffffffffffff, "65535.65535.65535.65535", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.255", FWUPD_VERSION_FORMAT_PAIR},
	    {0xffffffffffffffff, "4294967295.4294967295", FWUPD_VERSION_FORMAT_PAIR},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x11000000c8, "0x00000011000000c8", FWUPD_VERSION_FORMAT_HEX},
	};
	struct {
		guint16 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint16[] = {
	    {0x0, "0.0", FWUPD_VERSION_FORMAT_PAIR},
	    {0xff, "0.255", FWUPD_VERSION_FORMAT_PAIR},
	    {0xff01, "255.1", FWUPD_VERSION_FORMAT_PAIR},
	    {0x0, "0.0", FWUPD_VERSION_FORMAT_BCD},
	    {0x0110, "1.10", FWUPD_VERSION_FORMAT_BCD},
	    {0x9999, "99.99", FWUPD_VERSION_FORMAT_BCD},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x1234, "4660", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x1234, "1.2.52", FWUPD_VERSION_FORMAT_TRIPLET},
	};
	struct {
		const gchar *old;
		const gchar *new;
	} version_parse[] = {
	    {"0", "0"},
	    {"0x1a", "0.0.26"},
	    {"257", "0.0.257"},
	    {"1.2.3", "1.2.3"},
	    {"0xff0001", "0.255.1"},
	    {"16711681", "0.255.1"},
	    {"20150915", "20150915"},
	    {"dave", "dave"},
	    {"0x1x", "0x1x"},
	};

	/* check version conversion */
	for (i = 0; i < G_N_ELEMENTS(version_from_uint64); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint64(version_from_uint64[i].val,
					     version_from_uint64[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint64[i].ver);
	}
	for (i = 0; i < G_N_ELEMENTS(version_from_uint32); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint32(version_from_uint32[i].val,
					     version_from_uint32[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint32[i].ver);
	}
	for (i = 0; i < G_N_ELEMENTS(version_from_uint24); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint24(version_from_uint24[i].val,
					     version_from_uint24[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint24[i].ver);
	}
	for (i = 0; i < G_N_ELEMENTS(version_from_uint16); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint16(version_from_uint16[i].val,
					     version_from_uint16[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint16[i].ver);
	}

	/* check version parsing */
	for (i = 0; i < G_N_ELEMENTS(version_parse); i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_parse_from_format(version_parse[i].old,
						   FWUPD_VERSION_FORMAT_TRIPLET);
		g_assert_cmpstr(ver, ==, version_parse[i].new);
	}
}

static void
fu_common_vercmp_func(void)
{
	/* same */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN), ==, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.003", "001.002.003", FWUPD_VERSION_FORMAT_UNKNOWN),
	    ==,
	    0);
	g_assert_cmpint(fu_version_compare("0x00000002", "0x2", FWUPD_VERSION_FORMAT_HEX), ==, 0);

	/* upgrade and downgrade */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.4", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.000", "001.002.009", FWUPD_VERSION_FORMAT_UNKNOWN),
	    <,
	    0);
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.2", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.009", "001.002.000", FWUPD_VERSION_FORMAT_UNKNOWN),
	    >,
	    0);

	/* unequal depth */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3.1", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3.1", "1.2.4", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);

	/* mixed-alpha-numeric */
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3b", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3b", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha version append */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha only */
	g_assert_cmpint(fu_version_compare("alpha", "alpha", FWUPD_VERSION_FORMAT_UNKNOWN), ==, 0);
	g_assert_cmpint(fu_version_compare("alpha", "beta", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("beta", "alpha", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha-compare */
	g_assert_cmpint(fu_version_compare("1.2a.3", "1.2a.3", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2a.3", "1.2b.3", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2b.3", "1.2a.3", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* tilde is all-powerful */
	g_assert_cmpint(fu_version_compare("1.2.3~rc1", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3~rc1", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN),
			<,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3~rc2", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);

	/* invalid */
	g_assert_cmpint(fu_version_compare("1", NULL, FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
	g_assert_cmpint(fu_version_compare(NULL, "1", FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
	g_assert_cmpint(fu_version_compare(NULL, NULL, FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
}

static void
fu_firmware_raw_aligned_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("hello", 5);

	/* no alignment */
	ret = fu_firmware_parse(firmware1, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* invalid alignment */
	fu_firmware_set_alignment(firmware2, FU_FIRMWARE_ALIGNMENT_4K);
	ret = fu_firmware_parse(firmware2, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_firmware_ihex_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_hex = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_hex = NULL;
	g_autoptr(GError) error = NULL;

	/* load a Intel hex32 file */
	filename_hex = g_test_build_filename(G_TEST_DIST, "tests", "ihex.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename_hex, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	g_assert_cmpint(g_bytes_get_size(data_fw), ==, 92);

	/* export a ihex file (which will be slightly different due to
	 * non-continuous regions being expanded */
	data_hex = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_hex);
	data = g_bytes_get_data(data_hex, &len);
	str = g_strndup((const gchar *)data, len);
	g_assert_cmpstr(str,
			==,
			":100000004E6571756520706F72726F2071756973BE\n"
			":100010007175616D206573742071756920646F6CF2\n"
			":100020006F72656D20697073756D207175696120DF\n"
			":10003000646F6C6F722073697420616D65742C201D\n"
			":10004000636F6E73656374657475722C2061646987\n"
			":0C00500070697363692076656C69740A3E\n"
			":040000FD646176655F\n"
			":00000001FF\n");
}

static void
fu_firmware_ihex_signed_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;

	/* load a signed Intel hex32 file */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "ihex-signed.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	g_assert_cmpint(g_bytes_get_size(data_fw), ==, 11);

	/* get the signed image */
	data_sig = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_SIGNATURE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	data = g_bytes_get_data(data_sig, &len);
	g_assert_cmpint(len, ==, 8);
	g_assert_nonnull(data);
	g_assert_cmpint(memcmp(data, "deadbeef", 8), ==, 0);
}

static void
fu_firmware_ihex_offset_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(FuFirmware) firmware_verify = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_dummy = NULL;
	g_autoptr(GBytes) data_verify = NULL;
	g_autoptr(GError) error = NULL;

	/* add a 4 byte image in high memory */
	data_dummy = g_bytes_new_static("foo", 4);
	fu_firmware_set_addr(firmware, 0x80000000);
	fu_firmware_set_bytes(firmware, data_dummy);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	data = g_bytes_get_data(data_bin, &len);
	str = g_strndup((const gchar *)data, len);
	g_assert_cmpstr(str,
			==,
			":0200000480007A\n"
			":04000000666F6F00B8\n"
			":00000001FF\n");

	/* check we can load it too */
	ret = fu_firmware_parse(firmware_verify, data_bin, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_addr(firmware_verify), ==, 0x80000000);
	data_verify = fu_firmware_get_bytes(firmware_verify, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_verify);
	g_assert_cmpint(g_bytes_get_size(data_verify), ==, 0x4);
}

static void
fu_firmware_srec_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "srec.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 11);
}

static void
fu_firmware_fdt_func(void)
{
	gboolean ret;
	guint32 val32 = 0;
	guint64 val64 = 0;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *val = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_fdt_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFdtImage) img2 = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "fdt.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_fdt_firmware_get_cpuid(FU_FDT_FIRMWARE(firmware)), ==, 0x0);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	img1 = fu_firmware_get_image_by_id(firmware, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	ret = fu_fdt_image_get_attr_str(FU_FDT_IMAGE(img1), "key", &val, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(val, ==, "hello world");

	/* get image, and get the uint32 attr */
	img2 = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(firmware),
						 "/images/firmware-1",
						 &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	ret = fu_fdt_image_get_attr_u32(FU_FDT_IMAGE(img2), "key", &val32, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val32, ==, 0x123);

	/* wrong type */
	ret = fu_fdt_image_get_attr_u64(img2, "key", &val64, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_firmware_fit_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *str = NULL;
	g_auto(GStrv) val = NULL;
	g_autoptr(FuFdtImage) img1 = NULL;
	g_autoptr(FuFirmware) firmware = fu_fit_firmware_new();
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "fit.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_fit_firmware_get_timestamp(FU_FIT_FIRMWARE(firmware)), ==, 0x629D4ABD);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	img1 = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(firmware),
						 "/configurations/conf-1",
						 &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	ret = fu_fdt_image_get_attr_strlist(FU_FDT_IMAGE(img1),
					    FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
					    &val,
					    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_nonnull(val);
	g_assert_cmpstr(val[0], ==, "alice");
	g_assert_cmpstr(val[1], ==, "bob");
	g_assert_cmpstr(val[2], ==, "clara");
	g_assert_cmpstr(val[3], ==, NULL);
}

static void
fu_firmware_srec_tokenization_func(void)
{
	FuSrecFirmwareRecord *rcd;
	GPtrArray *records;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *buf = "S3060000001400E5\r\n"
			   "S31000000002281102000000007F0304002C\r\n"
			   "S306000000145095\r\n"
			   "S70500000000FA\r\n";
	data_srec = g_bytes_new_static(buf, strlen(buf));
	g_assert_no_error(error);
	g_assert_nonnull(data_srec);
	stream = g_memory_input_stream_new_from_bytes(data_srec);
	ret = fu_firmware_tokenize(firmware, stream, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	records = fu_srec_firmware_get_records(FU_SREC_FIRMWARE(firmware));
	g_assert_nonnull(records);
	g_assert_cmpint(records->len, ==, 4);
	rcd = g_ptr_array_index(records, 2);
	g_assert_nonnull(rcd);
	g_assert_cmpint(rcd->ln, ==, 0x3);
	g_assert_cmpint(rcd->kind, ==, 3);
	g_assert_cmpint(rcd->addr, ==, 0x14);
	g_assert_cmpint(rcd->buf->len, ==, 0x1);
	g_assert_cmpint(rcd->buf->data[0], ==, 0x50);
}

static void
fu_firmware_build_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *buf = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			   "<firmware>\n"
			   "  <version>1.2.3</version>\n"
			   "  <firmware>\n"
			   "    <version>4.5.6</version>\n"
			   "    <id>header</id>\n"
			   "    <idx>456</idx>\n"
			   "    <addr>0x456</addr>\n"
			   "    <data>aGVsbG8=</data>\n"
			   "  </firmware>\n"
			   "  <firmware>\n"
			   "    <version>7.8.9</version>\n"
			   "    <id>header</id>\n"
			   "    <idx>789</idx>\n"
			   "    <addr>0x789</addr>\n"
			   "  </firmware>\n"
			   "</firmware>\n";
	blob = g_bytes_new_static(buf, strlen(buf));
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* parse XML */
	ret = xb_builder_source_load_bytes(source, blob, XB_BUILDER_SOURCE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	n = xb_silo_query_first(silo, "firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(n);

	/* build object */
	ret = fu_firmware_build(firmware, n, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "1.2.3");

	/* verify image */
	img = fu_firmware_get_image_by_id(firmware, "header", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	g_assert_cmpstr(fu_firmware_get_version(img), ==, "4.5.6");
	g_assert_cmpint(fu_firmware_get_idx(img), ==, 456);
	g_assert_cmpint(fu_firmware_get_addr(img), ==, 0x456);
	blob2 = fu_firmware_write(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 5);
	str = g_strndup(g_bytes_get_data(blob2, NULL), g_bytes_get_size(blob2));
	g_assert_cmpstr(str, ==, "hello");
}

static gsize
fu_test_firmware_dfuse_image_get_size(FuFirmware *self)
{
	g_autoptr(GPtrArray) chunks = fu_firmware_get_chunks(self, NULL);
	gsize length = 0;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		length += fu_chunk_get_data_sz(chk);
	}
	return length;
}

static gsize
fu_test_firmware_dfuse_get_size(FuFirmware *firmware)
{
	gsize length = 0;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *image = g_ptr_array_index(images, i);
		length += fu_test_firmware_dfuse_image_get_size(image);
	}
	return length;
}

static void
fu_firmware_dfuse_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfuse_firmware_new();
	g_autoptr(GError) error = NULL;

	/* load a DfuSe firmware */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfuse.builder.xml", NULL);
	g_assert_nonnull(filename);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware)), ==, 0x1234);
	g_assert_cmpint(fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware)), ==, 0x5678);
	g_assert_cmpint(fu_dfu_firmware_get_release(FU_DFU_FIRMWARE(firmware)), ==, 0x8642);
	g_assert_cmpint(fu_test_firmware_dfuse_get_size(firmware), ==, 0x21);
}

static void
fu_firmware_fmap_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *img_str = NULL;
	g_autoptr(FuFirmware) firmware = fu_fmap_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;

#ifndef HAVE_MEMMEM
	g_test_skip("no memmem()");
	return;
#endif

	/* load firmware */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "fmap-offset.builder.xml", NULL);
	g_assert_nonnull(filename);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check image count */
	images = fu_firmware_get_images(firmware);
	g_assert_cmpint(images->len, ==, 2);

	/* get a specific image */
	img = fu_firmware_get_image_by_id(firmware, "FMAP", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	img_blob = fu_firmware_get_bytes(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_blob);
	g_assert_cmpint(g_bytes_get_size(img_blob), ==, 0xb);
	img_str = g_strndup(g_bytes_get_data(img_blob, NULL), g_bytes_get_size(img_blob));
	g_assert_cmpstr(img_str, ==, "hello world");

	/* can we roundtrip without losing data */
	roundtrip = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(roundtrip);
	csum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, roundtrip);
	g_assert_cmpstr(csum,
			==,
			"229fcd952264f42ae4853eda7e716cc5c1ae18e7f804a6ba39ab1dfde5737d7e");
}

static void
fu_firmware_new_from_gtypes_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new();
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) firmware3 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fw = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);
	stream = g_memory_input_stream_new_from_bytes(fw);
	g_assert_no_error(error);
	g_assert_nonnull(stream);

	/* dfu -> FuDfuFirmware */
	firmware1 = fu_firmware_new_from_gtypes(stream,
						0x0,
						FWUPD_INSTALL_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						FU_TYPE_DFUSE_FIRMWARE,
						FU_TYPE_DFU_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);
	g_assert_cmpstr(G_OBJECT_TYPE_NAME(firmware1), ==, "FuDfuFirmware");

	/* dfu -> FuFirmware */
	firmware2 = fu_firmware_new_from_gtypes(stream,
						0x0,
						FWUPD_INSTALL_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						FU_TYPE_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_no_error(error);
	g_assert_nonnull(firmware2);
	g_assert_cmpstr(G_OBJECT_TYPE_NAME(firmware2), ==, "FuFirmware");

	/* dfu -> error */
	firmware3 = fu_firmware_new_from_gtypes(stream,
						0x0,
						FWUPD_INSTALL_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware3);
}

static void
fu_firmware_csv_func(void)
{
	FuCsvEntry *entry_tmp;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_csv_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	const gchar *data =
	    "sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
	    "grub,1,Free Software Foundation,grub,2.04,https://www.gnu.org/software/grub/\n";

	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "component_generation");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_name");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_package_name");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_version");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_url");

	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 0), ==, "$id");
	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 1),
			==,
			"component_generation");
	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 5),
			==,
			"vendor_url");
	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 6), ==, NULL);

	blob = g_bytes_new(data, strlen(data));
	ret = fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	imgs = fu_firmware_get_images(firmware);
	g_assert_cmpint(imgs->len, ==, 2);

	entry_tmp = g_ptr_array_index(imgs, 1);

	g_assert_cmpstr(fu_firmware_get_id(FU_FIRMWARE(entry_tmp)), ==, "grub");
	g_assert_cmpstr(fu_csv_entry_get_value_by_idx(entry_tmp, 0), ==, NULL);
	g_assert_cmpstr(fu_csv_entry_get_value_by_idx(entry_tmp, 1), ==, "1");
	g_assert_cmpstr(fu_csv_entry_get_value_by_column_id(entry_tmp, "vendor_version"),
			==,
			"2.04");
}

static void
fu_firmware_archive_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) firmware = fu_archive_firmware_new();
	g_autoptr(FuFirmware) img_asc = NULL;
	g_autoptr(FuFirmware) img_bin = NULL;
	g_autoptr(FuFirmware) img_both = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	fn = g_test_build_filename(G_TEST_BUILT, "tests", "firmware.zip", NULL);
	file = g_file_new_for_path(fn);
	ret = fu_firmware_parse_file(firmware, file, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_archive_firmware_get_format(FU_ARCHIVE_FIRMWARE(firmware)),
			==,
			FU_ARCHIVE_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_archive_firmware_get_compression(FU_ARCHIVE_FIRMWARE(firmware)),
			==,
			FU_ARCHIVE_COMPRESSION_UNKNOWN);

	img_bin =
	    fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware), "*.txt", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_bin);
	img_asc = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware),
							"*.txt.asc",
							&error);
	g_assert_no_error(error);
	g_assert_nonnull(img_asc);
	img_both =
	    fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware), "*.txt*", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(img_both);
}

static void
fu_firmware_linear_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_linear_firmware_new(FU_TYPE_OPROM_FIRMWARE);
	g_autoptr(FuFirmware) firmware2 = fu_linear_firmware_new(FU_TYPE_OPROM_FIRMWARE);
	g_autoptr(GBytes) blob1 = g_bytes_new_static("XXXX", 4);
	g_autoptr(GBytes) blob2 = g_bytes_new_static("HELO", 4);
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(FuFirmware) img1 = fu_oprom_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_oprom_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	g_autofree gchar *str = NULL;

	/* add images then parse */
	fu_firmware_set_bytes(img1, blob1);
	fu_firmware_add_image(firmware1, img1);
	fu_firmware_set_bytes(img2, blob2);
	fu_firmware_add_image(firmware1, img2);
	blob3 = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob3);
	g_assert_cmpint(g_bytes_get_size(blob3), ==, 1024);

	/* parse them back */
	ret = fu_firmware_parse(firmware2, blob3, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_firmware_to_string(firmware2);
	g_debug("\n%s", str);

	/* verify we got both images */
	imgs = fu_firmware_get_images(firmware2);
	g_assert_cmpint(imgs->len, ==, 2);
}

static void
fu_firmware_dfu_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new();
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware)), ==, 0x1234);
	g_assert_cmpint(fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware)), ==, 0x4321);
	g_assert_cmpint(fu_dfu_firmware_get_release(FU_DFU_FIRMWARE(firmware)), ==, 0xdead);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 12);
}

static void
fu_firmware_ifwi_cpd_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_ifwi_cpd_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "ifwi-cpd.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_idx(firmware), ==, 0x1234);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 90);

	img1 = fu_firmware_get_image_by_id(firmware, "one", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 68);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 11);

	img2 = fu_firmware_get_image_by_id(firmware, "two", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	g_assert_cmpint(fu_firmware_get_offset(img2), ==, 79);
	g_assert_cmpint(fu_firmware_get_size(img2), ==, 11);
}

static void
fu_firmware_ifwi_fpt_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_ifwi_fpt_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "ifwi-fpt.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 118);

	img1 = fu_firmware_get_image_by_idx(firmware, 0x4f464e49, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 96);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 11);

	img2 = fu_firmware_get_image_by_idx(firmware, 0x4d495746, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	g_assert_cmpint(fu_firmware_get_offset(img2), ==, 107);
	g_assert_cmpint(fu_firmware_get_size(img2), ==, 11);
}

static void
fu_firmware_oprom_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_oprom_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_oprom_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "oprom.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware1, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_idx(firmware1), ==, 0x1);
	data_bin = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 1024);

	/* re-parse to get the CPD image */
	ret = fu_firmware_parse(firmware2, data_bin, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	img1 = fu_firmware_get_image_by_id(firmware2, "cpd", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 512);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 512);
}

static void
fu_firmware_dfu_patch_func(void)
{
	gboolean ret;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new();
	g_autoptr(GBytes) data_new = NULL;
	g_autoptr(GBytes) data_patch0 = g_bytes_new_static("XXXX", 4);
	g_autoptr(GBytes) data_patch1 = g_bytes_new_static("HELO", 4);
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a couple of patches */
	fu_firmware_add_patch(firmware, 0x0, data_patch0);
	fu_firmware_add_patch(firmware, 0x0, data_patch1);
	fu_firmware_add_patch(firmware, 0x8, data_patch1);

	data_new = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_new);
	fu_dump_full(G_LOG_DOMAIN,
		     "patch",
		     g_bytes_get_data(data_new, NULL),
		     g_bytes_get_size(data_new),
		     20,
		     FU_DUMP_FLAGS_SHOW_ASCII | FU_DUMP_FLAGS_SHOW_ADDRESSES);
	csum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_new);
	g_assert_cmpstr(csum, ==, "676c039e8cb1d2f51831fcb77be36db24bb8ecf8");
}

static void
fu_hid_descriptor_container_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_hid_descriptor_new();
	g_autoptr(FuFirmware) item_id = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "hid-descriptor2.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* find report-id from usage */
	report = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(firmware),
					       &error,
					       "usage-page",
					       0xFF02,
					       "usage",
					       0x01,
					       "feature",
					       0x02,
					       NULL);
	g_assert_no_error(error);
	g_assert_nonnull(report);
	item_id = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-id", &error);
	g_assert_no_error(error);
	g_assert_nonnull(item_id);
	g_assert_cmpint(fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id)), ==, 0x09);
}

static void
fu_hid_descriptor_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_hid_descriptor_new();
	g_autoptr(FuHidReport) report1 = NULL;
	g_autoptr(FuHidReport) report2 = NULL;
	g_autoptr(FuHidReport) report3 = NULL;
	g_autoptr(FuHidReport) report4 = NULL;
	g_autoptr(FuFirmware) item_usage = NULL;
	g_autoptr(FuFirmware) item_id = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "hid-descriptor.builder.xml", NULL);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* find report-id from usage */
	report4 =
	    fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(firmware), &error, "usage", 0xC8, NULL);
	g_assert_no_error(error);
	g_assert_nonnull(report4);

	item_id = fu_firmware_get_image_by_id(FU_FIRMWARE(report4), "report-id", &error);
	g_assert_no_error(error);
	g_assert_nonnull(item_id);
	g_assert_cmpint(fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id)), ==, 0xF1);

	/* find usage from report-id */
	report1 = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(firmware),
						&error,
						"report-id",
						0xF1,
						NULL);
	g_assert_no_error(error);
	g_assert_nonnull(report1);
	report2 = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(firmware),
						&error,
						"usage-page",
						0xFF0B,
						"report-id",
						0xF1,
						NULL);
	g_assert_no_error(error);
	g_assert_nonnull(report2);
	item_usage = fu_firmware_get_image_by_id(FU_FIRMWARE(report2), "usage", &error);
	g_assert_no_error(error);
	g_assert_nonnull(item_usage);
	g_assert_cmpint(fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_usage)), ==, 0xC8);

	/* not found */
	report3 = fu_hid_descriptor_find_report(FU_HID_DESCRIPTOR(firmware),
						&error,
						"usage-page",
						0x1234,
						"report-id",
						0xF1,
						NULL);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(report3);
}

static void
fu_firmware_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img1 = fu_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_firmware_new();
	g_autoptr(FuFirmware) img_id = NULL;
	g_autoptr(FuFirmware) img_idx = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;
	g_autofree gchar *str = NULL;

	fu_firmware_set_addr(img1, 0x200);
	fu_firmware_set_idx(img1, 13);
	fu_firmware_set_id(img1, "primary");
	fu_firmware_set_filename(img1, "BIOS.bin");
	fu_firmware_add_image(firmware, img1);
	fu_firmware_set_addr(img2, 0x400);
	fu_firmware_set_idx(img2, 23);
	fu_firmware_set_id(img2, "secondary");
	fu_firmware_add_image(firmware, img2);

	/* check depth */
	g_assert_cmpint(fu_firmware_get_depth(firmware), ==, 0);
	g_assert_cmpint(fu_firmware_get_depth(img1), ==, 1);
	g_assert_cmpint(fu_firmware_get_depth(img2), ==, 1);

	img_id = fu_firmware_get_image_by_id(firmware, "NotGoingToExist", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img_id);
	g_clear_error(&error);
	img_id = fu_firmware_get_image_by_id(firmware, "primary", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_id);
	g_assert_cmpint(fu_firmware_get_addr(img_id), ==, 0x200);
	g_assert_cmpint(fu_firmware_get_idx(img_id), ==, 13);
	g_assert_cmpstr(fu_firmware_get_id(img_id), ==, "primary");

	img_idx = fu_firmware_get_image_by_idx(firmware, 123456, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img_idx);
	g_clear_error(&error);
	img_idx = fu_firmware_get_image_by_idx(firmware, 23, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_idx);
	g_assert_cmpint(fu_firmware_get_addr(img_idx), ==, 0x400);
	g_assert_cmpint(fu_firmware_get_idx(img_idx), ==, 23);
	g_assert_cmpstr(fu_firmware_get_id(img_idx), ==, "secondary");

	str = fu_firmware_to_string(firmware);
	g_assert_cmpstr(str,
			==,
			"<firmware>\n"
			"  <firmware>\n"
			"    <id>primary</id>\n"
			"    <idx>0xd</idx>\n"
			"    <addr>0x200</addr>\n"
			"    <filename>BIOS.bin</filename>\n"
			"  </firmware>\n"
			"  <firmware>\n"
			"    <id>secondary</id>\n"
			"    <idx>0x17</idx>\n"
			"    <addr>0x400</addr>\n"
			"  </firmware>\n"
			"</firmware>\n");

	ret = fu_firmware_remove_image_by_idx(firmware, 0xd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_remove_image_by_id(firmware, "secondary", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	images = fu_firmware_get_images(firmware);
	g_assert_nonnull(images);
	g_assert_cmpint(images->len, ==, 0);
	ret = fu_firmware_remove_image_by_id(firmware, "NOTGOINGTOEXIST", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_firmware_convert_version_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_intel_thunderbolt_nvm_new();
	fu_firmware_set_version_raw(firmware, 0x1234);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "12.34");
}

static void
fu_firmware_common_func(void)
{
	gboolean ret;
	guint8 value = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_strparse_uint8_safe("ff00XX", 6, 0, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 0xFF);

	ret = fu_firmware_strparse_uint8_safe("ff00XX", 6, 2, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 0x00);

	ret = fu_firmware_strparse_uint8_safe("ff00XX", 6, 4, &value, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_firmware_dedupe_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img1 = fu_firmware_new();
	g_autoptr(FuFirmware) img1_old = fu_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_firmware_new();
	g_autoptr(FuFirmware) img2_old = fu_firmware_new();
	g_autoptr(FuFirmware) img3 = fu_firmware_new();
	g_autoptr(FuFirmware) img_id = NULL;
	g_autoptr(FuFirmware) img_idx = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_IDX);
	fu_firmware_set_images_max(firmware, 2);

	fu_firmware_set_idx(img1_old, 13);
	fu_firmware_set_id(img1_old, "DAVE");
	fu_firmware_add_image(firmware, img1_old);
	g_assert_true(fu_firmware_get_parent(img1_old) == firmware);

	fu_firmware_set_idx(img1, 13);
	fu_firmware_set_id(img1, "primary");
	fu_firmware_add_image(firmware, img1);

	fu_firmware_set_idx(img2_old, 123456);
	fu_firmware_set_id(img2_old, "secondary");
	fu_firmware_add_image(firmware, img2_old);

	fu_firmware_set_idx(img2, 23);
	fu_firmware_set_id(img2, "secondary");
	fu_firmware_add_image(firmware, img2);

	img_id = fu_firmware_get_image_by_id(firmware, "primary", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_id);
	g_assert_cmpint(fu_firmware_get_idx(img_id), ==, 13);
	g_assert_cmpstr(fu_firmware_get_id(img_id), ==, "primary");

	img_idx = fu_firmware_get_image_by_idx(firmware, 23, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_idx);
	g_assert_cmpint(fu_firmware_get_idx(img_idx), ==, 23);
	g_assert_cmpstr(fu_firmware_get_id(img_idx), ==, "secondary");

	ret = fu_firmware_add_image_full(firmware, img3, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_efivar_func(void)
{
	gboolean ret;
	gsize sz = 0;
	guint32 attr = 0;
	guint64 total;
	g_autofree guint8 *data = NULL;
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) names = NULL;

	/* check supported */
	ret = fu_efivars_supported(efivars, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write and read a key */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  (guint8 *)"1",
				  1,
				  FU_EFIVARS_ATTR_NON_VOLATILE | FU_EFIVARS_ATTR_RUNTIME_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  &data,
				  &sz,
				  &attr,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 1);
	g_assert_cmpint(attr, ==, FU_EFIVARS_ATTR_NON_VOLATILE | FU_EFIVARS_ATTR_RUNTIME_ACCESS);
	g_assert_cmpint(data[0], ==, '1');

	/* check existing keys */
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "NotGoingToExist"));
	g_assert_true(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));

	/* list a few keys */
	names = fu_efivars_get_names(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(names);
	g_assert_cmpint(names->len, ==, 1);

	/* check we can get the space used */
	total = fu_efivars_space_used(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, >=, 0x10);

	/* delete single key */
	ret = fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));
	g_assert_false(fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test", NULL));

	/* delete multiple keys */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test1",
				  (guint8 *)"1",
				  1,
				  0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test2",
				  (guint8 *)"1",
				  1,
				  0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_delete_with_glob(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test*", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test1"));
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test2"));

	/* read a key that doesn't exist */
	ret = fu_efivars_get_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "NotGoingToExist",
				  NULL,
				  NULL,
				  NULL,
				  &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_efivar_boot_func(void)
{
	FuFirmware *firmware_tmp;
	gboolean ret;
	const gchar *tmpdir = g_getenv("FWUPD_LOCALSTATEDIR");
	guint16 idx = 0;
	g_autofree gchar *pefile_fn = g_build_filename(tmpdir, "grubx64.efi", NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuEfiLoadOption) loadopt2 = NULL;
	g_autoptr(FuVolume) volume = fu_volume_new_from_mount_path(tmpdir);
	g_autoptr(GArray) bootorder2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(GPtrArray) esp_files = NULL;
	FuEfivars *efivars = fu_context_get_efivars(ctx);

	/* set and get BootCurrent */
	ret = fu_efivars_set_boot_current(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_boot_current(efivars, &idx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(idx, ==, 0x0001);

	/* set and get BootNext */
	ret = fu_efivars_set_boot_next(efivars, 0x0002, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_boot_next(efivars, &idx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(idx, ==, 0x0002);

	/* set and get BootOrder */
	ret = fu_efivars_build_boot_order(efivars, &error, 0x0001, 0x0002, G_MAXUINT16);
	g_assert_no_error(error);
	g_assert_true(ret);
	bootorder2 = fu_efivars_get_boot_order(efivars, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bootorder2);
	g_assert_cmpint(bootorder2->len, ==, 2);
	idx = g_array_index(bootorder2, guint16, 0);
	g_assert_cmpint(idx, ==, 0x0001);
	idx = g_array_index(bootorder2, guint16, 1);
	g_assert_cmpint(idx, ==, 0x0002);

	/* add a plausible ESP */
	fu_volume_set_partition_kind(volume, FU_VOLUME_KIND_ESP);
	fu_volume_set_partition_uuid(volume, "41f5e9b7-eb4f-5c65-b8a6-f94b0ad54815");
	fu_context_add_esp_volume(ctx, volume);

	/* create Boot0001 and Boot0002 */
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0001,
						      volume,
						      "Fedora",
						      "grubx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0002,
						      volume,
						      "Firmware Update",
						      "fwupdx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check BootXXXX exists */
	loadopt2 = fu_efivars_get_boot_entry(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_nonnull(loadopt2);
	entries = fu_efivars_get_boot_entries(efivars, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bootorder2);
	g_assert_cmpint(bootorder2->len, ==, 2);

	/* check we detected something */
	esp_files =
	    fu_context_get_esp_files(ctx, FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(esp_files);
	g_assert_cmpint(esp_files->len, ==, 2);
	firmware_tmp = g_ptr_array_index(esp_files, 0);
	g_assert_cmpstr(fu_firmware_get_filename(firmware_tmp), ==, pefile_fn);
}

typedef struct {
	guint cnt_success;
	guint cnt_failed;
} FuDeviceRetryHelper;

static gboolean
fu_device_retry_success(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_success++;
	return TRUE;
}

static gboolean
fu_device_retry_failed(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static gboolean
fu_device_retry_success_3rd_try(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	if (helper->cnt_failed == 2) {
		helper->cnt_success++;
		return TRUE;
	}
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static void
fu_device_retry_success_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_failed);
	ret = fu_device_retry(device, fu_device_retry_success, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 0);
}

static void
fu_device_retry_failed_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_success);
	ret = fu_device_retry(device, fu_device_retry_failed, 3, &helper, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_true(!ret);
	g_assert_cmpint(helper.cnt_success, ==, 2); /* do not reset for the last failure */
	g_assert_cmpint(helper.cnt_failed, ==, 3);
}

static void
fu_device_retry_hardware_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	ret = fu_device_retry(device, fu_device_retry_success_3rd_try, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 2);
}

static void
fu_bios_settings_load_func(void)
{
	gboolean ret;
	gint integer;
	const gchar *tmp;
	GPtrArray *values;
	FwupdBiosSetting *setting;
	FwupdBiosSettingKind kind;
	g_autofree gchar *base_dir = NULL;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FuBiosSettings) p620_6_3_settings = NULL;
	g_autoptr(FuBiosSettings) xp29310_settings = NULL;
	g_autoptr(GPtrArray) p620_6_3_items = NULL;
	g_autoptr(GPtrArray) xps9310_items = NULL;

#ifdef _WIN32
	/* the "AlarmDate(MM\DD\YYYY)" setting really confuses wine for obvious reasons */
	g_test_skip("BIOS settings not supported on Windows");
	return;
#endif

	/* ensure the data directory is actually present for the test */
	base_dir = g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", NULL);
	if (!g_file_test(base_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	/* load BIOS settings from a Lenovo P620 (with thinklmi driver problems) */
	test_dir = g_build_filename(base_dir, "lenovo-p620", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
	g_clear_error(&error);
	g_free(test_dir);

	/* load BIOS settings from a Lenovo P620 running 6.3 */
	test_dir = g_build_filename(base_dir, "lenovo-p620-6.3", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	p620_6_3_settings = fu_context_get_bios_settings(ctx);
	p620_6_3_items = fu_bios_settings_get_all(p620_6_3_settings);
	g_assert_cmpint(p620_6_3_items->len, ==, 5);

	/* make sure nothing pending */
	ret = fu_context_get_bios_setting_pending_reboot(ctx);
	g_assert_false(ret);

	/* check a BIOS setting reads from kernel 6.3 as expected by fwupd */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.AMDMemoryGuard");
	g_assert_nonnull(setting);
	tmp = fwupd_bios_setting_get_name(setting);
	g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
	tmp = fwupd_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
	tmp = fwupd_bios_setting_get_current_value(setting);
	g_assert_cmpstr(tmp, ==, "Disable");
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Disable");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Enable");
	}

	/* try to read an BIOS setting known to have ][Status] to make sure we worked
	 * around the thinklmi bug sufficiently
	 */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.StartupSequence");
	g_assert_nonnull(setting);
	tmp = fwupd_bios_setting_get_current_value(setting);
	g_assert_cmpstr(tmp, ==, "Primary");
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Primary");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Automatic");
	}

	/* check BIOS settings that should be read only */
	for (guint i = 0; i < p620_6_3_items->len; i++) {
		const gchar *name;
		gboolean ro;

		setting = g_ptr_array_index(p620_6_3_items, i);
		ro = fwupd_bios_setting_get_read_only(setting);
		tmp = fwupd_bios_setting_get_current_value(setting);
		name = fwupd_bios_setting_get_name(setting);
		g_debug("%s: %s", name, tmp);
		if ((g_strcmp0(name, "pending_reboot") == 0) || (g_strrstr(tmp, "[Status") != NULL))
			g_assert_true(ro);
		else
			g_assert_false(ro);
	}

	g_free(test_dir);

	/* load BIOS settings from a Lenovo P14s Gen1 */
	test_dir = g_build_filename(base_dir, "lenovo-p14s-gen1", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);
	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
	g_clear_error(&error);
	g_free(test_dir);

	/* load BIOS settings from a Dell XPS 9310 */
	test_dir = g_build_filename(base_dir, "dell-xps13-9310", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);
	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	xp29310_settings = fu_context_get_bios_settings(ctx);
	xps9310_items = fu_bios_settings_get_all(xp29310_settings);
	g_assert_cmpint(xps9310_items->len, ==, 109);

	/* make sure that we DIDN'T parse reset_bios setting */
	setting = fu_context_get_bios_setting(ctx, FWUPD_BIOS_SETTING_RESET_BIOS);
	g_assert_null(setting);

	/* look at a integer BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.CustomChargeStop");
	g_assert_nonnull(setting);
	kind = fwupd_bios_setting_get_kind(setting);
	g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_INTEGER);
	integer = fwupd_bios_setting_get_lower_bound(setting);
	g_assert_cmpint(integer, ==, 55);
	integer = fwupd_bios_setting_get_upper_bound(setting);
	g_assert_cmpint(integer, ==, 100);
	integer = fwupd_bios_setting_get_scalar_increment(setting);
	g_assert_cmpint(integer, ==, 1);

	/* look at a string BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.Asset");
	g_assert_nonnull(setting);
	integer = fwupd_bios_setting_get_lower_bound(setting);
	g_assert_cmpint(integer, ==, 1);
	integer = fwupd_bios_setting_get_upper_bound(setting);
	g_assert_cmpint(integer, ==, 64);
	tmp = fwupd_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "Asset Tag");

	/* look at a enumeration BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.BiosRcvrFrmHdd");
	g_assert_nonnull(setting);
	kind = fwupd_bios_setting_get_kind(setting);
	g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Disabled");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Enabled");
	}

	/* make sure we defaulted UEFI Secure boot to read only if enabled */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.SecureBoot");
	g_assert_nonnull(setting);
	ret = fwupd_bios_setting_get_read_only(setting);
	g_assert_true(ret);
}

static void
fu_security_attrs_hsi_func(void)
{
	g_autofree gchar *hsi1 = NULL;
	g_autofree gchar *hsi2 = NULL;
	g_autofree gchar *hsi3 = NULL;
	g_autofree gchar *hsi4 = NULL;
	g_autofree gchar *hsi5 = NULL;
	g_autofree gchar *hsi6 = NULL;
	g_autofree gchar *hsi7 = NULL;
	g_autofree gchar *hsi8 = NULL;
	g_autofree gchar *hsi9 = NULL;
	g_autofree gchar *expected_hsi9 = NULL;
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no attrs */
	attrs = fu_security_attrs_new();
	hsi1 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi1, ==, "HSI:0");

	/* just success from HSI:1 */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi2 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi2, ==, "HSI:1");
	g_clear_object(&attr);

	/* add failed from HSI:2, so still HSI:1 */
	attr = fwupd_security_attr_new("org.fwupd.hsi.PRX");
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi3 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi3, ==, "HSI:1");
	g_clear_object(&attr);

	/* add an implicit obsolete via duplication */
	attr = fwupd_security_attr_new("org.fwupd.hsi.PRX");
	fwupd_security_attr_set_plugin(attr, "other-plugin");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_url(attr, "http://other-plugin");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi4 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi4, ==, "HSI:1");
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	g_clear_object(&attr);

	/* add attr from HSI:3, obsoleting the failure */
	attr = fwupd_security_attr_new("org.fwupd.hsi.BIOSGuard");
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_obsolete(attr, "org.fwupd.hsi.PRX");
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi5 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi5, ==, "HSI:3");
	g_clear_object(&attr);

	/* add taint that was fine */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi6 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi6, ==, "HSI:3");
	g_clear_object(&attr);

	/* add updates and attestation */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi7 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi7, ==, "HSI:3");
	g_clear_object(&attr);

	/* add issue that was uncool */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi8 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi8, ==, "HSI:3!");
	g_clear_object(&attr);

	/* show version in the attribute */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi9 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_ADD_VERSION);
	expected_hsi9 = g_strdup_printf("HSI:3! (v%d.%d.%d)",
					FWUPD_MAJOR_VERSION,
					FWUPD_MINOR_VERSION,
					FWUPD_MICRO_VERSION);
	g_assert_cmpstr(hsi9, ==, expected_hsi9);
	g_clear_object(&attr);
}

static void
fu_security_attrs_compare_func(void)
{
	FwupdSecurityAttr *attr_tmp;
	g_autoptr(FuSecurityAttrs) attrs1 = fu_security_attrs_new();
	g_autoptr(FuSecurityAttrs) attrs2 = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr1 = fwupd_security_attr_new("org.fwupd.hsi.foo");
	g_autoptr(FwupdSecurityAttr) attr2 = fwupd_security_attr_new("org.fwupd.hsi.bar");
	g_autoptr(FwupdSecurityAttr) attr3 = fwupd_security_attr_new("org.fwupd.hsi.baz");
	g_autoptr(FwupdSecurityAttr) attr4 = fwupd_security_attr_new("org.fwupd.hsi.baz");
	g_autoptr(GPtrArray) results = NULL;

	/* attrs1 has foo and baz(enabled) */
	fwupd_security_attr_set_plugin(attr1, "foo");
	fwupd_security_attr_set_created(attr1, 0);
	fwupd_security_attr_set_result(attr1, FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);
	fu_security_attrs_append(attrs1, attr1);
	fwupd_security_attr_set_plugin(attr3, "baz");
	fwupd_security_attr_set_created(attr3, 0);
	fwupd_security_attr_set_result(attr3, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs1, attr3);

	/* attrs2 has bar and baz(~enabled) */
	fwupd_security_attr_set_plugin(attr2, "bar");
	fwupd_security_attr_set_created(attr2, 0);
	fwupd_security_attr_set_result(attr2, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs2, attr2);
	fwupd_security_attr_set_plugin(attr4, "baz");
	fwupd_security_attr_set_created(attr4, 0);
	fwupd_security_attr_set_result(attr4, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	fu_security_attrs_append(attrs2, attr4);

	results = fu_security_attrs_compare(attrs1, attrs2);
	g_assert_cmpint(results->len, ==, 3);
	attr_tmp = g_ptr_array_index(results, 0);
	g_assert_cmpstr(fwupd_security_attr_get_appstream_id(attr_tmp), ==, "org.fwupd.hsi.bar");
	g_assert_cmpint(fwupd_security_attr_get_result_fallback(attr_tmp),
			==,
			FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_tmp),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	attr_tmp = g_ptr_array_index(results, 1);
	g_assert_cmpstr(fwupd_security_attr_get_appstream_id(attr_tmp), ==, "org.fwupd.hsi.foo");
	g_assert_cmpint(fwupd_security_attr_get_result_fallback(attr_tmp),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_tmp),
			==,
			FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
	attr_tmp = g_ptr_array_index(results, 2);
	g_assert_cmpstr(fwupd_security_attr_get_appstream_id(attr_tmp), ==, "org.fwupd.hsi.baz");
	g_assert_cmpint(fwupd_security_attr_get_result_fallback(attr_tmp),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_tmp),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);

	g_assert_true(fu_security_attrs_equal(attrs1, attrs1));
	g_assert_false(fu_security_attrs_equal(attrs1, attrs2));
	g_assert_false(fu_security_attrs_equal(attrs2, attrs1));
}

static void
fu_firmware_builder_round_trip_func(void)
{
	struct {
		GType gtype;
		const gchar *xml_fn;
		const gchar *checksum;
	} map[] = {
	    {FU_TYPE_CAB_FIRMWARE, "cab.builder.xml", "a708f47b1a46377f1ea420597641ffe9a40abd75"},
	    {FU_TYPE_CAB_FIRMWARE, "cab-compressed.builder.xml", NULL}, /* not byte-identical */
	    {FU_TYPE_ELF_FIRMWARE, "elf.builder.xml", "99ea60b8dd46085dcbf1ecd5e72b4cb73a3b6faa"},
	    {FU_TYPE_DFUSE_FIRMWARE,
	     "dfuse.builder.xml",
	     "c1ff429f0e381c8fe8e1b2ee41a5a9a79e2f2ff7"},
	    {FU_TYPE_PEFILE_FIRMWARE,
	     "pefile.builder.xml",
	     "73b0e0dc9f6175b7bc27b77f20e0d9eca2d2d141"},
	    {FU_TYPE_LINEAR_FIRMWARE,
	     "linear.builder.xml",
	     "18fa8201652c82dc717df1905d8ab72e46e3d82b"},
	    {FU_TYPE_HID_REPORT_ITEM,
	     "hid-report-item.builder.xml",
	     "5b18c07399fc8968ce22127df38d8d923089ec92"},
	    {FU_TYPE_HID_DESCRIPTOR,
	     "hid-descriptor.builder.xml",
	     "6bb23f7c9fedc21f05528b3b63ad5837f4a16a92"},
	    {FU_TYPE_SBATLEVEL_SECTION,
	     "sbatlevel.builder.xml",
	     "8204ef9477b4305748a0de6e667547cb6ce5e426"},
	    {FU_TYPE_CSV_FIRMWARE, "csv.builder.xml", "986cbf8cde5bc7d8b49ee94cceae3f92efbd2eef"},
	    {FU_TYPE_FDT_FIRMWARE, "fdt.builder.xml", "40f7fbaff684a6bcf67c81b3079422c2529741e1"},
	    {FU_TYPE_FIT_FIRMWARE, "fit.builder.xml", "293ce07351bb7d76631c4e2ba47243db1e150f3c"},
	    {FU_TYPE_SREC_FIRMWARE, "srec.builder.xml", "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_IHEX_FIRMWARE, "ihex.builder.xml", "a8d74f767f3fc992b413e5ba801cedc80a4cf013"},
	    {FU_TYPE_FMAP_FIRMWARE, "fmap.builder.xml", "a0b9ffc10a586d217edf9e9bae7c1fe7c564ea01"},
	    {FU_TYPE_EFI_LOAD_OPTION,
	     "efi-load-option.builder.xml",
	     "7ef696d22902ae97ef5f73ad9c85a28095ad56f1"},
	    {FU_TYPE_EDID, "edid.builder.xml", "64cef10b75ccce684a483d576dd4a4ce6bef8165"},
	    {FU_TYPE_EFI_SECTION,
	     "efi-section.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_EFI_SECTION,
	     "efi-section.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_EFI_FILE, "efi-file.builder.xml", "90374d97cf6bc70059d24c816c188c10bd250ed7"},
	    {FU_TYPE_EFI_FILESYSTEM,
	     "efi-filesystem.builder.xml",
	     "d6fbadc1c303a3b4eede9db7fb0ddb353efffc86"},
	    {FU_TYPE_EFI_SIGNATURE_LIST,
	     "efi-signature-list.builder.xml",
	     "b3a46ac55847336a7d74fdf6957fd86ca193b1d9"},
	    {FU_TYPE_EFI_VOLUME,
	     "efi-volume.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_IFD_FIRMWARE, "ifd.builder.xml", "06ae066ea53cefe43fed2f1ca4fc7d8cccdbcf1e"},
	    {FU_TYPE_CFU_OFFER,
	     "cfu-offer.builder.xml",
	     "c10223887ff6cdf4475ad07c65b1f0f3a2d0d5ca"},
	    {FU_TYPE_CFU_PAYLOAD,
	     "cfu-payload.builder.xml",
	     "5da829f5fd15a28970aed98ebb26ebf2f88ed6f2"},
	    {FU_TYPE_IFWI_CPD_FIRMWARE,
	     "ifwi-cpd.builder.xml",
	     "91e348d17cb91ef7a528e85beb39d15a0532dca5"},
	    {FU_TYPE_IFWI_FPT_FIRMWARE,
	     "ifwi-fpt.builder.xml",
	     "d1f0fb2c2a7a99441bf4a825d060642315a94d91"},
	    {FU_TYPE_OPROM_FIRMWARE,
	     "oprom.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_INTEL_THUNDERBOLT_NVM,
	     "intel-thunderbolt.builder.xml",
	     "b3a73baf05078dfdd833b407a0a6afb239ec2f23"},
#ifdef HAVE_CBOR
	    {FU_TYPE_USWID_FIRMWARE,
	     "uswid.builder.xml",
	     "b473fbdbe00f860c4da43f9499569394bac81f14"},
	    {FU_TYPE_USWID_FIRMWARE, "uswid-compressed.builder.xml", NULL}, /* not byte-identical */
#endif
	    {G_TYPE_INVALID, NULL, NULL}};
	g_type_ensure(FU_TYPE_COSWID_FIRMWARE);
	for (guint i = 0; map[i].gtype != G_TYPE_INVALID; i++) {
		gboolean ret;
		g_autofree gchar *csum1 = NULL;
		g_autofree gchar *csum2 = NULL;
		g_autofree gchar *filename = NULL;
		g_autofree gchar *xml1 = NULL;
		g_autofree gchar *xml2 = NULL;
		g_autoptr(FuFirmware) firmware1 = g_object_new(map[i].gtype, NULL);
		g_autoptr(FuFirmware) firmware2 = g_object_new(map[i].gtype, NULL);
		g_autoptr(FuFirmware) firmware3 = g_object_new(map[i].gtype, NULL);
		g_autoptr(GError) error = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* build and write */
		filename = g_test_build_filename(G_TEST_DIST, "tests", map[i].xml_fn, NULL);
		ret = g_file_get_contents(filename, &xml1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		ret = fu_firmware_build_from_xml(firmware1, xml1, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum1);
		if (map[i].checksum != NULL)
			g_assert_cmpstr(csum1, ==, map[i].checksum);

		/* ensure we can write and then parse what we just wrote */
		blob = fu_firmware_write(firmware1, &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);
		ret = fu_firmware_parse(firmware3, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
		if (!ret)
			g_prefix_error(&error, "%s: ", map[i].xml_fn);
		g_assert_no_error(error);
		g_assert_true(ret);

		/* ensure we can round-trip */
		xml2 = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
		g_assert_no_error(error);
		ret = fu_firmware_build_from_xml(firmware2, xml2, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
		g_assert_nonnull(csum2);
		g_assert_no_error(error);
		if (map[i].checksum != NULL)
			g_assert_cmpstr(csum2, ==, map[i].checksum);
	}
}

typedef struct {
	guint last_percentage;
	guint updates;
} FuProgressHelper;

static void
fu_progress_percentage_changed_cb(FuProgress *progress, guint percentage, gpointer data)
{
	FuProgressHelper *helper = (FuProgressHelper *)data;
	helper->last_percentage = percentage;
	helper->updates++;
}

static void
fu_progress_func(void)
{
	FuProgressHelper helper = {0};
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autofree gchar *str = NULL;

	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	g_assert_cmpfloat_with_epsilon(fu_progress_get_duration(progress), 0.f, 0.001);

	fu_progress_set_profile(progress, TRUE);
	fu_progress_set_steps(progress, 5);
	g_assert_cmpint(helper.last_percentage, ==, 0);

	g_usleep(20 * 1000);
	fu_progress_step_done(progress);
	g_assert_cmpint(helper.updates, ==, 2);
	g_assert_cmpint(helper.last_percentage, ==, 20);

	for (guint i = 0; i < 4; i++) {
		g_usleep(20 * 1000);
		fu_progress_step_done(progress);
	}

	g_assert_cmpint(helper.last_percentage, ==, 100);
	g_assert_cmpint(helper.updates, ==, 6);
	g_assert_cmpfloat_with_epsilon(fu_progress_get_duration(progress), 0.1f, 0.05);
	str = fu_progress_traceback(progress);
	g_debug("\n%s", str);
}

static void
fu_progress_child_func(void)
{
	FuProgressHelper helper = {0};
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* reset */
	fu_progress_set_profile(progress, TRUE);
	fu_progress_set_steps(progress, 2);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	/* parent: |-----------------------|-----------------------|
	 * step1:  |-----------------------|
	 * child:                          |-------------|---------|
	 */

	/* PARENT UPDATE */
	g_debug("parent update #1");
	fu_progress_step_done(progress);
	g_assert_cmpint(helper.updates, ==, 1);
	g_assert_cmpint(helper.last_percentage, ==, 50);

	/* now test with a child */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);

	g_debug("child update #1");
	fu_progress_step_done(child);
	g_assert_cmpint(helper.updates, ==, 2);
	g_assert_cmpint(helper.last_percentage, ==, 75);

	/* child update */
	g_debug("child update #2");
	fu_progress_step_done(child);
	g_assert_cmpint(helper.updates, ==, 3);
	g_assert_cmpint(helper.last_percentage, ==, 100);

	/* parent update */
	g_debug("parent update #2");
	fu_progress_step_done(progress);

	/* ensure we ignored the duplicate */
	g_assert_cmpint(helper.updates, ==, 3);
	g_assert_cmpint(helper.last_percentage, ==, 100);
}

static void
fu_progress_scaling_func(void)
{
	const guint insane_steps = 1000 * 1000;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	fu_progress_set_steps(progress, insane_steps);
	for (guint i = 0; i < insane_steps / 2; i++)
		fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 50);
	for (guint i = 0; i < insane_steps / 2; i++) {
		FuProgress *progress_child = fu_progress_get_child(progress);
		fu_progress_set_percentage(progress_child, 0);
		fu_progress_set_percentage(progress_child, 100);
		fu_progress_step_done(progress);
	}
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 100);
}

static void
fu_progress_parent_one_step_proxy_func(void)
{
	FuProgressHelper helper = {0};
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* one step */
	fu_progress_set_steps(progress, 1);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	/* now test with a child */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);

	/* child set value */
	fu_progress_set_percentage(child, 33);

	/* ensure 1 updates for progress with one step and ensure using child value as parent */
	g_assert_cmpint(helper.updates, ==, 1);
	g_assert_cmpint(helper.last_percentage, ==, 33);
}

static void
fu_progress_non_equal_steps_func(void)
{
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	FuProgress *child;
	FuProgress *grandchild;

	/* test non-equal steps */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 60, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 20, NULL);
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 0);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_ERASE);

	/* child step should increment according to the custom steps */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);
	fu_progress_set_status(child, FWUPD_STATUS_DEVICE_BUSY);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_BUSY);

	/* start child */
	fu_progress_step_done(child);

	/* verify 10% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 10);

	/* finish child */
	fu_progress_step_done(child);

	/* ensure the parent is switched back to the status before the child took over */
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_ERASE);

	fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_WRITE);

	/* verify 20% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 20);

	/* child step should increment according to the custom steps */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_add_step(child, FWUPD_STATUS_DEVICE_RESTART, 25, NULL);
	fu_progress_add_step(child, FWUPD_STATUS_DEVICE_WRITE, 75, NULL);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_RESTART);

	/* start child */
	fu_progress_step_done(child);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_WRITE);

	/* verify bilinear interpolation is working */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 35);

	/*
	 * 0        20                             80         100
	 * |---------||----------------------------||---------|
	 *            |       35                   |
	 *            |-------||-------------------| (25%)
	 *                     |              75.5 |
	 *                     |---------------||--| (90%)
	 */
	grandchild = fu_progress_get_child(child);
	fu_progress_set_id(grandchild, G_STRLOC);
	fu_progress_add_step(grandchild, FWUPD_STATUS_DEVICE_ERASE, 90, NULL);
	fu_progress_add_step(grandchild, FWUPD_STATUS_DEVICE_WRITE, 10, NULL);

	fu_progress_step_done(grandchild);

	/* verify bilinear interpolation (twice) is working for subpercentage */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 75);

	fu_progress_step_done(grandchild);

	/* finish child */
	fu_progress_step_done(child);

	fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_READ);

	/* verify 80% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 80);

	fu_progress_step_done(progress);

	/* verify 100% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 100);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_UNKNOWN);
}

static void
fu_progress_finish_func(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* check straight finish */
	fu_progress_set_steps(progress, 3);

	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 3);
	fu_progress_finished(child);

	/* parent step done after child finish */
	fu_progress_step_done(progress);
}

static void
fu_progress_child_finished(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* check straight finish */
	fu_progress_set_steps(progress, 3);

	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 3);
	/* some imaginary ignorable error */

	/* parent step done after child finish */
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
	fu_progress_step_done(progress);
}

static void
fu_partial_input_stream_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[5] = {0x0};
	goffset pos;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	/*                                             \--/   */
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) base_stream = g_memory_input_stream_new_from_bytes(blob);
	g_autoptr(GInputStream) stream_complete = NULL;
	g_autoptr(GInputStream) stream_error = NULL;
	g_autoptr(GInputStream) stream_file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* check the behavior of GFileInputStream */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	g_assert_nonnull(fn);
	file = g_file_new_for_path(fn);
	stream_file = G_INPUT_STREAM(g_file_read(file, NULL, &error));
	g_assert_no_error(error);
	g_assert_nonnull(stream_file);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 0x0);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 216);
	rc = g_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	pos = g_seekable_tell(G_SEEKABLE(stream_file));
	g_assert_cmpint(pos, ==, 216);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), pos, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 216);
	/* we CAN seek past the end... */
	ret = g_seekable_seek(G_SEEKABLE(stream_file), pos + 10000, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 10216);
	/* reads all return zero */
	rc = g_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	/* END offset is negative */
	ret = g_seekable_seek(G_SEEKABLE(stream_file), -0x1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream_file, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 10);

	/* check the behavior of GMemoryInputStream */
	g_assert_no_error(error);
	g_assert_nonnull(stream_file);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x0);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	rc = g_input_stream_read(base_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	pos = g_seekable_tell(G_SEEKABLE(base_stream));
	g_assert_cmpint(pos, ==, 8);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), pos, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(base_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	/* we CANNOT seek past the end... */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), pos + 10000, G_SEEK_SET, NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_assert_false(ret);
	g_clear_error(&error);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	/* END offset is negative */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(base_stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, '8');

	/* seek to non-start */
	stream = fu_partial_input_stream_new(base_stream, 2, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x2);

	/* read from start */
	rc = g_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '5');
	g_assert_cmpint(buf[1], ==, '6');
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x4);
	rc = g_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* convert back to bytes */
	blob2 = fu_input_stream_read_bytes(stream, 0x0, G_MAXUINT32, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 4);

	/* seek to end of base stream */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x8);
	rc = g_input_stream_read(base_stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x8);

	/* seek to end of partial stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x4);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to offset to end of partial stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x3);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, '6');

	/* attempt an overread of the base stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);

	/* attempt to seek way past the base stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x1000, G_SEEK_SET, NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read right up against the end of the base stream */
	stream_complete = fu_partial_input_stream_new(base_stream, 0, 8, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream_complete);
	ret = g_seekable_seek(G_SEEKABLE(stream_complete), 0x8, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream_complete, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* try to create an out-of-range partial stream */
	stream_error = fu_partial_input_stream_new(base_stream, 0, 9, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(stream_error);
}

static void
fu_composite_input_stream_func(void)
{
	gboolean ret;
	gsize streamsz = 0;
	gssize rc;
	guint8 buf[2] = {0x0};
	g_autofree gchar *str = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob1 = g_bytes_new_static("ab", 2);
	g_autoptr(GBytes) blob2 = g_bytes_new_static("cde", 3);
	g_autoptr(GBytes) blob3 = g_bytes_new_static("xxxfgyyy", 8);
	g_autoptr(GBytes) blob4 = NULL;
	g_autoptr(GInputStream) composite_stream = fu_composite_input_stream_new();
	g_autoptr(GInputStream) stream3 = g_memory_input_stream_new_from_bytes(blob3);
	g_autoptr(GInputStream) stream4 = NULL;

	/* empty */
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 0);

	/* add bytes */
	fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(composite_stream), blob1);
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 2);

	/* add bytes */
	fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(composite_stream), blob2);
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 5);

	/* add partial stream */
	stream4 = fu_partial_input_stream_new(stream3, 0x3, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream4);
	fu_composite_input_stream_add_partial_stream(FU_COMPOSITE_INPUT_STREAM(composite_stream),
						     FU_PARTIAL_INPUT_STREAM(stream4));
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 7);

	/* to string */
	str = fwupd_codec_to_string(FWUPD_CODEC(composite_stream));
	g_print("%s", str);

	/* first block */
	ret = fu_input_stream_read_safe(composite_stream,
					buf,
					sizeof(buf),
					0x0, /* offset */
					0x0, /* seek */
					sizeof(buf),
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'a');
	g_assert_cmpint(buf[1], ==, 'b');

	/* indented into second block */
	ret = fu_input_stream_read_safe(composite_stream,
					buf,
					sizeof(buf),
					0x0, /* offset */
					0x3, /* seek */
					sizeof(buf),
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'd');
	g_assert_cmpint(buf[1], ==, 'e');

	/* third input stream has an offset */
	ret = fu_input_stream_read_safe(composite_stream,
					buf,
					sizeof(buf),
					0x0, /* offset */
					0x5, /* seek */
					sizeof(buf),
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'f');
	g_assert_cmpint(buf[1], ==, 'g');

	/* read across a boundary, so should return early */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), 0x1, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(composite_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'b');

	/* seek to end of composite stream */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x7);
	rc = g_input_stream_read(composite_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to the same place directly */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), 0x7, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x7);
	rc = g_input_stream_read(composite_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to offset to end of composite stream */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x6);
	rc = g_input_stream_read(composite_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'g');

	/* dump entire composite stream */
	blob4 = fu_input_stream_read_bytes(composite_stream, 0x0, G_MAXUINT32, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob4);
	g_assert_cmpint(g_bytes_get_size(blob4), ==, 7);
	g_assert_cmpint(memcmp(g_bytes_get_data(blob4, NULL), "abcdefg", 7), ==, 0);
}

static gboolean
fu_strsplit_stream_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	guint *cnt = (guint *)user_data;
	g_debug(">%s<", token->str);
	(*cnt)++;
	return TRUE;
}

static void
fu_strsplit_stream_func(void)
{
	gboolean ret;
	guint cnt1 = 0;
	guint cnt2 = 0;
	guint cnt3 = 0;
	const gchar str1[] = "simple string";
	const gchar str2[] = "123delimited123start123and123end123";
	const gchar str3[] = "this|has|trailing|nuls\0\0\0\0";
	g_autoptr(GInputStream) stream1 = NULL;
	g_autoptr(GInputStream) stream2 = NULL;
	g_autoptr(GInputStream) stream3 = NULL;
	g_autoptr(GError) error = NULL;

	/* check includes NUL */
	g_assert_cmpint(sizeof(str1), ==, 14);

	stream1 = G_INPUT_STREAM(g_memory_input_stream_new_from_data(str1, sizeof(str1), NULL));
	ret = fu_strsplit_stream(stream1, 0x0, " ", fu_strsplit_stream_cb, &cnt1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt1, ==, 2);

	stream2 = G_INPUT_STREAM(g_memory_input_stream_new_from_data(str2, sizeof(str2), NULL));
	ret = fu_strsplit_stream(stream2, 0x0, "123", fu_strsplit_stream_cb, &cnt2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt2, ==, 6);

	stream3 = G_INPUT_STREAM(g_memory_input_stream_new_from_data(str3, sizeof(str3), NULL));
	ret = fu_strsplit_stream(stream3, 0x0, "|", fu_strsplit_stream_cb, &cnt3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt3, ==, 4);
}

static void
fu_input_stream_find_func(void)
{
	const gchar *haystack = "I write free software. Firmware troublemaker.";
	const gchar *needle1 = "Firmware";
	const gchar *needle2 = "XXX";
	gboolean ret;
	gsize offset = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	stream =
	    g_memory_input_stream_new_from_data((const guint8 *)haystack, strlen(haystack), NULL);
	ret =
	    fu_input_stream_find(stream, (const guint8 *)needle1, strlen(needle1), &offset, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(offset, ==, 23);

	ret =
	    fu_input_stream_find(stream, (const guint8 *)needle2, strlen(needle2), &offset, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_input_stream_chunkify_func(void)
{
	gboolean ret;
	guint8 sum8 = 0;
	guint16 crc16 = 0x0;
	guint32 crc32 = 0xffffffff;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *checksum2 = NULL;

	for (guint i = 0; i < 0x80000; i++)
		fu_byte_array_append_uint8(buf, i);
	blob = g_bytes_new(buf->data, buf->len);
	stream = g_memory_input_stream_new_from_bytes(blob);

	ret = fu_input_stream_compute_sum8(stream, &sum8, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sum8, ==, fu_sum8_bytes(blob));

	checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, blob);
	g_assert_cmpstr(checksum, ==, checksum2);

	ret = fu_input_stream_compute_crc16(stream, FU_CRC_KIND_B16_XMODEM, &crc16, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc16, ==, fu_crc16(FU_CRC_KIND_B16_XMODEM, buf->data, buf->len));

	ret = fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &crc32, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc32, ==, fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len));
}

static void
fu_lzma_func(void)
{
	gboolean ret;
	g_autoptr(GByteArray) buf_in = g_byte_array_new();
	g_autoptr(GBytes) blob_in = NULL;
	g_autoptr(GBytes) blob_orig = NULL;
	g_autoptr(GBytes) blob_out = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LZMA
	g_test_skip("not compiled with lzma support");
	return;
#endif

	/* create a repeating pattern */
	for (guint i = 0; i < 10000; i++) {
		guint8 tmp = i % 8;
		g_byte_array_append(buf_in, &tmp, sizeof(tmp));
	}
	blob_in = g_bytes_new(buf_in->data, buf_in->len);

	/* compress */
	blob_out = fu_lzma_compress_bytes(blob_in, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_out);
	g_assert_cmpint(g_bytes_get_size(blob_out), <, 500);

	/* decompress */
	blob_orig = fu_lzma_decompress_bytes(blob_out, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_orig);
	ret = fu_bytes_compare(blob_in, blob_orig, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_efi_lz77_decompressor_func(void)
{
	gboolean ret;
	g_autofree gchar *csum_legacy = NULL;
	g_autofree gchar *csum_tiano = NULL;
	g_autofree gchar *filename_legacy = NULL;
	g_autofree gchar *filename_tiano = NULL;
	g_autoptr(FuFirmware) lz77_decompressor_legacy = fu_efi_lz77_decompressor_new();
	g_autoptr(FuFirmware) lz77_decompressor_tiano = fu_efi_lz77_decompressor_new();
	g_autoptr(GBytes) blob_legacy2 = NULL;
	g_autoptr(GBytes) blob_legacy = NULL;
	g_autoptr(GBytes) blob_tiano2 = NULL;
	g_autoptr(GBytes) blob_tiano = NULL;
	g_autoptr(GError) error = NULL;

	filename_tiano = g_test_build_filename(G_TEST_DIST, "tests", "efi-lz77-tiano.bin", NULL);
	blob_tiano = fu_bytes_get_contents(filename_tiano, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_tiano);
	g_assert_cmpint(g_bytes_get_size(blob_tiano), ==, 144);
	ret =
	    fu_firmware_parse(lz77_decompressor_tiano, blob_tiano, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob_tiano2 = fu_firmware_get_bytes(lz77_decompressor_tiano, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_tiano2);
	g_assert_cmpint(g_bytes_get_size(blob_tiano2), ==, 276);
	csum_tiano = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, blob_tiano2);
	g_assert_cmpstr(csum_tiano, ==, "40f7fbaff684a6bcf67c81b3079422c2529741e1");

	filename_legacy = g_test_build_filename(G_TEST_DIST, "tests", "efi-lz77-legacy.bin", NULL);
	blob_legacy = fu_bytes_get_contents(filename_legacy, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_legacy);
	g_assert_cmpint(g_bytes_get_size(blob_legacy), ==, 144);
	ret = fu_firmware_parse(lz77_decompressor_legacy,
				blob_tiano,
				FWUPD_INSTALL_FLAG_NONE,
				&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob_legacy2 = fu_firmware_get_bytes(lz77_decompressor_legacy, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_legacy2);
	g_assert_cmpint(g_bytes_get_size(blob_legacy2), ==, 276);
	csum_legacy = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, blob_legacy2);
	g_assert_cmpstr(csum_legacy, ==, "40f7fbaff684a6bcf67c81b3079422c2529741e1");
}

static void
fu_input_stream_func(void)
{
	gboolean ret;
	gsize bufsz = 0;
	gsize streamsz = 0;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf2 = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	g_assert_nonnull(fn);
	ret = g_file_get_contents(fn, (gchar **)&buf, &bufsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_dump_raw(G_LOG_DOMAIN, "src", buf, bufsz);
	csum = g_compute_checksum_for_data(G_CHECKSUM_MD5, (const guchar *)buf, bufsz);

	file = g_file_new_for_path(fn);
	stream = G_INPUT_STREAM(g_file_read(file, NULL, &error));
	g_assert_no_error(error);
	g_assert_nonnull(stream);

	/* verify size */
	ret = fu_input_stream_size(stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, bufsz);

	/* verify checksum */
	csum2 = fu_input_stream_compute_checksum(stream, G_CHECKSUM_MD5, &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum2);
	g_assert_cmpstr(csum, ==, csum2);

	/* read first byte */
	buf2 = g_malloc0(bufsz);
	ret = fu_input_stream_read_safe(stream, buf2, bufsz, 0x0, 0x0, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, buf2[0]);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);

	/* read bytes 2,3 */
	ret = fu_input_stream_read_safe(stream,
					buf2,
					bufsz,
					0x1, /* offset */
					0x1, /* seek */
					2,   /* count */
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);
	g_assert_cmpint(buf[1], ==, buf2[1]);
	g_assert_cmpint(buf[2], ==, buf2[2]);

	/* read past end of stream */
	ret = fu_input_stream_read_safe(stream,
					buf2,
					bufsz,
					0x0,   /* offset */
					0x20,  /* seek */
					bufsz, /* count */
					&error);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

static void
fu_plugin_struct_bits_func(void)
{
	g_autofree gchar *str1 = NULL;
	g_autoptr(FuStructSelfTestBits) st2 = NULL;
	g_autoptr(FuStructSelfTestBits) st = fu_struct_self_test_bits_new();
	g_autoptr(GError) error = NULL;

	/* 0b1111 + 0b1 + 0b0010 = 0b111110010 -> 0x1F2 */
	g_assert_cmpint(st->len, ==, 4);
	fu_dump_raw(G_LOG_DOMAIN, "buf", st->data, st->len);
	g_assert_cmpint(st->data[0], ==, 0xF2);
	g_assert_cmpint(st->data[1], ==, 0x01);
	g_assert_cmpint(st->data[2], ==, 0x0);
	g_assert_cmpint(st->data[3], ==, 0x0);

	st2 = fu_struct_self_test_bits_parse(st->data, st->len, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);

	g_assert_cmpint(fu_struct_self_test_bits_get_lower(st2), ==, 0x2);
	g_assert_cmpint(fu_struct_self_test_bits_get_middle(st2), ==, 0b1);
	g_assert_cmpint(fu_struct_self_test_bits_get_upper(st2), ==, 0xF);

	str1 = fu_struct_self_test_bits_to_string(st2);
	g_assert_cmpstr(str1,
			==,
			"FuStructSelfTestBits:\n"
			"  lower: 0x2 [two]\n"
			"  middle: 0x1\n"
			"  upper: 0xf");

	/* set all to maximum value */
	fu_struct_self_test_bits_set_lower(st2, G_MAXUINT32);
	fu_struct_self_test_bits_set_middle(st2, G_MAXUINT32);
	fu_struct_self_test_bits_set_upper(st2, G_MAXUINT32);
	g_assert_cmpint(fu_struct_self_test_bits_get_lower(st2), ==, 0xF);
	g_assert_cmpint(fu_struct_self_test_bits_get_middle(st2), ==, 0x1);
	g_assert_cmpint(fu_struct_self_test_bits_get_upper(st2), ==, 0xF);
}

static void
fu_plugin_struct_func(void)
{
	gboolean ret;
	g_autoptr(GByteArray) st = fu_struct_self_test_new();
	g_autoptr(GByteArray) st2 = NULL;
	g_autoptr(GByteArray) st3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *oem_table_id = NULL;

	/* size */
	g_assert_cmpint(st->len, ==, 51);

	/* getters and setters */
	fu_struct_self_test_set_revision(st, 0xFF);
	fu_struct_self_test_set_length(st, 0xDEAD);
	ret = fu_struct_self_test_set_oem_table_id(st, "X", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_struct_self_test_get_revision(st), ==, 0xFF);
	g_assert_cmpint(fu_struct_self_test_get_length(st), ==, 0xDEAD);

	/* pack */
	str1 = fu_byte_array_to_string(st);
	g_assert_cmpstr(str1,
			==,
			"12345678adde0000ff000000000000000000000000000000004142434445465800000000"
			"00000000000000dfdfdfdf00000000");

	/* parse */
	st2 = fu_struct_self_test_parse(st->data, st->len, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);
	g_assert_cmpint(fu_struct_self_test_get_revision(st2), ==, 0xFF);
	g_assert_cmpint(fu_struct_self_test_get_length(st2), ==, 0xDEAD);
	oem_table_id = fu_struct_self_test_get_oem_table_id(st2);
	g_assert_cmpstr(oem_table_id, ==, "X");

	/* to string */
	str2 = fu_struct_self_test_to_string(st);
	g_assert_cmpstr(str2,
			==,
			"FuStructSelfTest:\n"
			"  length: 0xdead\n"
			"  revision: 0xff [all]\n"
			"  owner: 00000000-0000-0000-0000-000000000000\n"
			"  oem_table_id: X\n"
			"  oem_revision: 0x0\n"
			"  asl_compiler_id: 0xDFDFDFDF\n"
			"  asl_compiler_revision: 0x0");

	/* parse failing signature */
	st->data[0] = 0xFF;
	st3 = fu_struct_self_test_parse(st->data, st->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(st3);
	g_clear_error(&error);
	ret = fu_struct_self_test_validate(st->data, st->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_plugin_struct_wrapped_func(void)
{
	gboolean ret;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *str4 = NULL;
	g_autoptr(GByteArray) st2 = NULL;
	g_autoptr(GByteArray) st3 = NULL;
	g_autoptr(GByteArray) st_base2 = NULL;
	g_autoptr(GByteArray) st_base = fu_struct_self_test_new();
	g_autoptr(GByteArray) st = fu_struct_self_test_wrapped_new();
	g_autoptr(GError) error = NULL;

	/* size */
	g_assert_cmpint(st->len, ==, 53);

	/* getters and setters */
	fu_struct_self_test_wrapped_set_less(st, 0x99);
	fu_struct_self_test_wrapped_set_more(st, 0x12);
	g_assert_cmpint(fu_struct_self_test_wrapped_get_more(st), ==, 0x12);
	str1 = fu_byte_array_to_string(st);
	g_assert_cmpstr(str1,
			==,
			"991234567833000000000000000000000000000000000000000041424344454600000000"
			"0000000000000000dfdfdfdf0000000012");

	/* modify the base */
	fu_struct_self_test_set_revision(st_base, 0xFE);
	ret = fu_struct_self_test_wrapped_set_base(st, st_base, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str4 = fu_byte_array_to_string(st);
	g_assert_cmpstr(str4,
			==,
			"991234567833000000fe0000000000000000000000000000000041424344454600000000"
			"0000000000000000dfdfdfdf0000000012");

	/* parse */
	st2 = fu_struct_self_test_wrapped_parse(st->data, st->len, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);
	g_assert_cmpint(fu_struct_self_test_wrapped_get_more(st), ==, 0x12);
	st_base2 = fu_struct_self_test_wrapped_get_base(st);
	g_assert_cmpint(fu_struct_self_test_get_revision(st_base2), ==, 0xFE);

	/* to string */
	str2 = fu_struct_self_test_wrapped_to_string(st);
	g_assert_cmpstr(str2,
			==,
			"FuStructSelfTestWrapped:\n"
			"  less: 0x99\n"
			"  base: FuStructSelfTest:\n"
			"  length: 0x33\n"
			"  revision: 0xfe\n"
			"  owner: 00000000-0000-0000-0000-000000000000\n"
			"  oem_revision: 0x0\n"
			"  asl_compiler_id: 0xDFDFDFDF\n"
			"  asl_compiler_revision: 0x0\n"
			"  more: 0x12");

	/* parse failing signature */
	st->data[FU_STRUCT_SELF_TEST_WRAPPED_OFFSET_BASE] = 0xFF;
	st3 = fu_struct_self_test_wrapped_parse(st->data, st->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(st3);
	g_clear_error(&error);
	ret = fu_struct_self_test_wrapped_validate(st->data, st->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_efi_load_option_func(void)
{
	g_autoptr(FuEfivars) efivars = fu_efivars_new();
	/*
	 * 0000 = Linux-Firmware-Updater
	 * 0001 = Fedora
	 * 0002 = Windows Boot Manager
	 */
	for (guint16 i = 0; i < 3; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(FuEfiLoadOption) load_option =
		    fu_efivars_get_boot_entry(efivars, i, &error);
		g_autoptr(GBytes) fw = NULL;
		g_autofree gchar *str = NULL;

		if (load_option == NULL) {
			g_debug("failed: %s", error->message);
			continue;
		}
		str = fu_firmware_to_string(FU_FIRMWARE(load_option));
		g_debug("%s", str);
		fw = fu_firmware_write(FU_FIRMWARE(load_option), &error);
		g_assert_no_error(error);
		g_assert_nonnull(fw);
	}
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_IFD_BIOS);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_DATADIR_VENDOR_IDS", testdatadir, TRUE);
	(void)g_setenv("FWUPD_LIBDIR_PKG", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSDMIDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", "/tmp/fwupd-self-test/var", TRUE);
	(void)g_setenv("FWUPD_PROFILE", "1", TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);
	(void)g_setenv("CACHE_DIRECTORY", "/tmp/fwupd-self-test/cache", TRUE);

	g_test_add_func("/fwupd/efi-lz77{decompressor}", fu_efi_lz77_decompressor_func);
	g_test_add_func("/fwupd/input-stream", fu_input_stream_func);
	g_test_add_func("/fwupd/input-stream{chunkify}", fu_input_stream_chunkify_func);
	g_test_add_func("/fwupd/input-stream{find}", fu_input_stream_find_func);
	g_test_add_func("/fwupd/partial-input-stream", fu_partial_input_stream_func);
	g_test_add_func("/fwupd/composite-input-stream", fu_composite_input_stream_func);
	g_test_add_func("/fwupd/struct", fu_plugin_struct_func);
	g_test_add_func("/fwupd/struct{bits}", fu_plugin_struct_bits_func);
	g_test_add_func("/fwupd/struct{wrapped}", fu_plugin_struct_wrapped_func);
	g_test_add_func("/fwupd/plugin{quirks-append}", fu_plugin_quirks_append_func);
	g_test_add_func("/fwupd/quirks{vendor-ids}", fu_quirks_vendor_ids_func);
	g_test_add_func("/fwupd/string{password-mask}", fu_strpassmask_func);
	g_test_add_func("/fwupd/string{strsplit-stream}", fu_strsplit_stream_func);
	g_test_add_func("/fwupd/lzma", fu_lzma_func);
	g_test_add_func("/fwupd/common{strnsplit}", fu_strsplit_func);
	g_test_add_func("/fwupd/common{olson-timezone-id}", fu_common_olson_timezone_id_func);
	g_test_add_func("/fwupd/common{memmem}", fu_common_memmem_func);
	if (g_test_slow())
		g_test_add_func("/fwupd/progress", fu_progress_func);
	g_test_add_func("/fwupd/progress{scaling}", fu_progress_scaling_func);
	g_test_add_func("/fwupd/progress{child}", fu_progress_child_func);
	g_test_add_func("/fwupd/progress{child-finished}", fu_progress_child_finished);
	g_test_add_func("/fwupd/progress{parent-1-step}", fu_progress_parent_one_step_proxy_func);
	g_test_add_func("/fwupd/progress{no-equal}", fu_progress_non_equal_steps_func);
	g_test_add_func("/fwupd/progress{finish}", fu_progress_finish_func);
	g_test_add_func("/fwupd/bios-attrs{load}", fu_bios_settings_load_func);
	g_test_add_func("/fwupd/security-attrs{hsi}", fu_security_attrs_hsi_func);
	g_test_add_func("/fwupd/security-attrs{compare}", fu_security_attrs_compare_func);
	g_test_add_func("/fwupd/config", fu_config_func);
	g_test_add_func("/fwupd/plugin", fu_plugin_func);
	g_test_add_func("/fwupd/plugin{vfuncs}", fu_plugin_vfuncs_func);
	g_test_add_func("/fwupd/plugin{device-gtype}", fu_plugin_device_gtype_func);
	g_test_add_func("/fwupd/plugin{backend-device}", fu_plugin_backend_device_func);
	g_test_add_func("/fwupd/plugin{backend-proxy-device}", fu_plugin_backend_proxy_device_func);
	g_test_add_func("/fwupd/plugin{config}", fu_plugin_config_func);
	g_test_add_func("/fwupd/plugin{devices}", fu_plugin_devices_func);
	g_test_add_func("/fwupd/plugin{device-inhibit-children}",
			fu_plugin_device_inhibit_children_func);
	g_test_add_func("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func("/fwupd/plugin{quirks}", fu_plugin_quirks_func);
	g_test_add_func("/fwupd/plugin{fdt}", fu_plugin_fdt_func);
	g_test_add_func("/fwupd/plugin{quirks-performance}", fu_plugin_quirks_performance_func);
	g_test_add_func("/fwupd/plugin{quirks-device}", fu_plugin_quirks_device_func);
	g_test_add_func("/fwupd/backend", fu_backend_func);
	g_test_add_func("/fwupd/backend{emulate}", fu_backend_emulate_func);
	g_test_add_func("/fwupd/chunk", fu_chunk_func);
	g_test_add_func("/fwupd/chunks", fu_chunk_array_func);
	g_test_add_func("/fwupd/common{align-up}", fu_common_align_up_func);
	g_test_add_func("/fwupd/volume{gpt-type}", fu_volume_gpt_type_func);
	g_test_add_func("/fwupd/common{bitwise}", fu_common_bitwise_func);
	g_test_add_func("/fwupd/common{byte-array}", fu_common_byte_array_func);
	g_test_add_func("/fwupd/common{crc}", fu_common_crc_func);
	g_test_add_func("/fwupd/common{string-append-kv}", fu_string_append_func);
	g_test_add_func("/fwupd/common{version-guess-format}", fu_version_guess_format_func);
	g_test_add_func("/fwupd/common{strtoull}", fu_strtoull_func);
	g_test_add_func("/fwupd/common{strtoll}", fu_strtoll_func);
	g_test_add_func("/fwupd/common{version}", fu_common_version_func);
	g_test_add_func("/fwupd/common{version-semver}", fu_version_semver_func);
	g_test_add_func("/fwupd/common{vercmp}", fu_common_vercmp_func);
	g_test_add_func("/fwupd/common{strstrip}", fu_strstrip_func);
	g_test_add_func("/fwupd/common{endian}", fu_common_endian_func);
	g_test_add_func("/fwupd/common{bytes-get-data}", fu_common_bytes_get_data_func);
	g_test_add_func("/fwupd/common{kernel-lockdown}", fu_common_kernel_lockdown_func);
	g_test_add_func("/fwupd/common{strsafe}", fu_strsafe_func);
	g_test_add_func("/fwupd/msgpack", fu_msgpack_func);
	g_test_add_func("/fwupd/msgpack{binary-stream}", fu_msgpack_binary_stream_func);
	g_test_add_func("/fwupd/msgpack{lookup}", fu_msgpack_lookup_func);
	g_test_add_func("/fwupd/efi-load-option", fu_efi_load_option_func);
	g_test_add_func("/fwupd/efivar", fu_efivar_func);
	g_test_add_func("/fwupd/efivar{bootxxxx}", fu_efivar_boot_func);
	g_test_add_func("/fwupd/hwids", fu_hwids_func);
	g_test_add_func("/fwupd/context{flags}", fu_context_flags_func);
	g_test_add_func("/fwupd/context{backends}", fu_context_backends_func);
	g_test_add_func("/fwupd/context{hwids-dmi}", fu_context_hwids_dmi_func);
	g_test_add_func("/fwupd/context{firmware-gtypes}", fu_context_firmware_gtypes_func);
	g_test_add_func("/fwupd/context{state}", fu_context_state_func);
	g_test_add_func("/fwupd/string{utf16}", fu_string_utf16_func);
	g_test_add_func("/fwupd/smbios", fu_smbios_func);
	g_test_add_func("/fwupd/smbios3", fu_smbios3_func);
	g_test_add_func("/fwupd/kernel{cmdline}", fu_kernel_cmdline_func);
	g_test_add_func("/fwupd/kernel{config}", fu_kernel_config_func);
	g_test_add_func("/fwupd/hid{descriptor}", fu_hid_descriptor_func);
	g_test_add_func("/fwupd/hid{descriptor-container}", fu_hid_descriptor_container_func);
	g_test_add_func("/fwupd/firmware", fu_firmware_func);
	g_test_add_func("/fwupd/firmware{common}", fu_firmware_common_func);
	g_test_add_func("/fwupd/firmware{convert-version}", fu_firmware_convert_version_func);
	g_test_add_func("/fwupd/firmware{csv}", fu_firmware_csv_func);
	g_test_add_func("/fwupd/firmware{archive}", fu_firmware_archive_func);
	g_test_add_func("/fwupd/firmware{linear}", fu_firmware_linear_func);
	g_test_add_func("/fwupd/firmware{dedupe}", fu_firmware_dedupe_func);
	g_test_add_func("/fwupd/firmware{build}", fu_firmware_build_func);
	g_test_add_func("/fwupd/firmware{raw-aligned}", fu_firmware_raw_aligned_func);
	g_test_add_func("/fwupd/firmware{ihex}", fu_firmware_ihex_func);
	g_test_add_func("/fwupd/firmware{ihex-offset}", fu_firmware_ihex_offset_func);
	g_test_add_func("/fwupd/firmware{ihex-signed}", fu_firmware_ihex_signed_func);
	g_test_add_func("/fwupd/firmware{srec-tokenization}", fu_firmware_srec_tokenization_func);
	g_test_add_func("/fwupd/firmware{srec}", fu_firmware_srec_func);
	g_test_add_func("/fwupd/firmware{fdt}", fu_firmware_fdt_func);
	g_test_add_func("/fwupd/firmware{fit}", fu_firmware_fit_func);
	g_test_add_func("/fwupd/firmware{ifwi-cpd}", fu_firmware_ifwi_cpd_func);
	g_test_add_func("/fwupd/firmware{ifwi-fpt}", fu_firmware_ifwi_fpt_func);
	g_test_add_func("/fwupd/firmware{oprom}", fu_firmware_oprom_func);
	g_test_add_func("/fwupd/firmware{dfu}", fu_firmware_dfu_func);
	g_test_add_func("/fwupd/firmware{dfu-patch}", fu_firmware_dfu_patch_func);
	g_test_add_func("/fwupd/firmware{dfuse}", fu_firmware_dfuse_func);
	g_test_add_func("/fwupd/firmware{builder-round-trip}", fu_firmware_builder_round_trip_func);
	g_test_add_func("/fwupd/firmware{fmap}", fu_firmware_fmap_func);
	g_test_add_func("/fwupd/firmware{gtypes}", fu_firmware_new_from_gtypes_func);
	g_test_add_func("/fwupd/archive{invalid}", fu_archive_invalid_func);
	g_test_add_func("/fwupd/archive{cab}", fu_archive_cab_func);
	g_test_add_func("/fwupd/device", fu_device_func);
	g_test_add_func("/fwupd/device{event}", fu_device_event_func);
	g_test_add_func("/fwupd/device{event-donor}", fu_device_event_donor_func);
	g_test_add_func("/fwupd/device{vfuncs}", fu_device_vfuncs_func);
	g_test_add_func("/fwupd/device{instance-ids}", fu_device_instance_ids_func);
	g_test_add_func("/fwupd/device{composite-id}", fu_device_composite_id_func);
	g_test_add_func("/fwupd/device{flags}", fu_device_flags_func);
	g_test_add_func("/fwupd/device{private-flags}", fu_device_custom_flags_func);
	g_test_add_func("/fwupd/device{inhibit}", fu_device_inhibit_func);
	g_test_add_func("/fwupd/device{inhibit-updateable}", fu_device_inhibit_updateable_func);
	g_test_add_func("/fwupd/device{parent}", fu_device_parent_func);
	g_test_add_func("/fwupd/device{children}", fu_device_children_func);
	g_test_add_func("/fwupd/device{incorporate}", fu_device_incorporate_func);
	g_test_add_func("/fwupd/device{incorporate-flag}", fu_device_incorporate_flag_func);
	g_test_add_func("/fwupd/device{incorporate-descendant}",
			fu_device_incorporate_descendant_func);
	g_test_add_func("/fwupd/device{poll}", fu_device_poll_func);
	g_test_add_func("/fwupd/device-locker{success}", fu_device_locker_func);
	g_test_add_func("/fwupd/device-locker{fail}", fu_device_locker_fail_func);
	g_test_add_func("/fwupd/device{name}", fu_device_name_func);
	g_test_add_func("/fwupd/device{metadata}", fu_device_metadata_func);
	g_test_add_func("/fwupd/device{open-refcount}", fu_device_open_refcount_func);
	g_test_add_func("/fwupd/device{version-format}", fu_device_version_format_func);
	g_test_add_func("/fwupd/device{retry-success}", fu_device_retry_success_func);
	g_test_add_func("/fwupd/device{retry-failed}", fu_device_retry_failed_func);
	g_test_add_func("/fwupd/device{retry-hardware}", fu_device_retry_hardware_func);
	g_test_add_func("/fwupd/device{cfi-device}", fu_device_cfi_device_func);
	g_test_add_func("/fwupd/device{progress}", fu_plugin_device_progress_func);
	return g_test_run();
}
