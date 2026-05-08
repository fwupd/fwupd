/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-error.h"
#include "fwupd-jcat-blob.h"
#include "fwupd-jcat-file.h"
#include "fwupd-jcat-item.h"
#include "fwupd-test.h"

static void
fwupd_jcat_blob_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *str_perfect = "FwupdJcatBlob:\n"
				   "  Kind:                 gpg\n"
				   "  Target:               sha256\n"
				   "  Flags:                is-utf8\n"
				   "  Timestamp:            1970-01-01T03:25:45Z\n"
				   "  Size:                 0x5\n"
				   "  Data:                 BEGIN\n";

	/* enums */
	for (guint i = FWUPD_JCAT_BLOB_KIND_UNKNOWN + 1; i < FWUPD_JCAT_BLOB_KIND_LAST; i++) {
		const gchar *tmp = fwupd_jcat_blob_kind_to_string(i);
		g_assert_nonnull(tmp);
		g_assert_cmpint(fwupd_jcat_blob_kind_from_string(tmp), ==, i);
	}

	/* sanity check */
	blob = fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_GPG, "BEGIN");
	g_assert_cmpint(fwupd_jcat_blob_get_kind(blob), ==, FWUPD_JCAT_BLOB_KIND_GPG);
	g_assert_nonnull(fwupd_jcat_blob_get_data(blob));
	fwupd_jcat_blob_set_timestamp(blob, 12345);
	g_assert_cmpint(fwupd_jcat_blob_get_timestamp(blob), ==, 12345);
	fwupd_jcat_blob_set_target(blob, FWUPD_JCAT_BLOB_KIND_SHA256);
	g_assert_cmpint(fwupd_jcat_blob_get_target(blob), ==, FWUPD_JCAT_BLOB_KIND_SHA256);

	/* to string */
	str = fwupd_codec_to_string(FWUPD_CODEC(blob));
	g_debug("%s", str);
	ret = fu_test_compare_lines(str, str_perfect, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_jcat_item_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdJcatItem) item = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *str_perfect = "FwupdJcatItem:\n"
				   "  ID:                   filename.bin\n"
				   "  AliasId:              foo.bin\n";

	/* sanity check */
	item = fwupd_jcat_item_new("filename.bin");
	fwupd_jcat_item_add_alias_id(item, "foo.bin");
	fwupd_jcat_item_add_alias_id(item, "bar.bin");
	fwupd_jcat_item_remove_alias_id(item, "bar.bin");
	g_assert_cmpstr(fwupd_jcat_item_get_id(item), ==, "filename.bin");

	/* to string */
	str = fwupd_codec_to_string(FWUPD_CODEC(item));
	g_debug("%s", str);
	ret = fu_test_compare_lines(str, str_perfect, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_jcat_file_json_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdJcatFile) file1 = fwupd_jcat_file_new();
	g_autoptr(FwupdJcatFile) file2 = fwupd_jcat_file_new();
	g_autoptr(GError) error = NULL;

	json1 = fwupd_jcat_file_export_json(file1, FWUPD_CODEC_FLAG_NO_TIMESTAMP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);

	ret = fwupd_jcat_file_import_json(file2, json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fwupd_codec_to_string(FWUPD_CODEC(file2));
	g_debug("%s", str);
	json2 = fwupd_jcat_file_export_json(file2, FWUPD_CODEC_FLAG_NO_TIMESTAMP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);

	ret = fu_test_compare_lines(json1, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_jcat_file_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) data = g_bytes_new("hello world", 12);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) blobs0 = NULL;
	g_autoptr(GPtrArray) blobs1 = NULL;
	g_autoptr(GPtrArray) blobs2 = NULL;
	g_autoptr(GPtrArray) blobs3 = NULL;
	g_autoptr(GPtrArray) items0 = NULL;
	g_autoptr(GPtrArray) items1 = NULL;
	g_autoptr(FwupdJcatBlob) blob1 = NULL;
	g_autoptr(FwupdJcatBlob) blob2 = NULL;
	g_autoptr(FwupdJcatFile) file2 = fwupd_jcat_file_new();
	g_autoptr(FwupdJcatFile) file = fwupd_jcat_file_new();
	g_autoptr(FwupdJcatItem) item1 = NULL;
	g_autoptr(FwupdJcatItem) item2 = NULL;
	g_autoptr(FwupdJcatItem) item3 = NULL;
	g_autoptr(FwupdJcatItem) item4 = NULL;
	g_autoptr(FwupdJcatItem) item = fwupd_jcat_item_new("firmware.bin");
	const gchar *json_perfect = "{\n"
				    "  \"JcatVersionMajor\": 0,\n"
				    "  \"JcatVersionMinor\": 1,\n"
				    "  \"Items\": [\n"
				    "    {\n"
				    "      \"Id\": \"firmware.bin\",\n"
				    "      \"AliasIds\": [\n"
				    "        \"foo.bin\"\n"
				    "      ],\n"
				    "      \"Blobs\": [\n"
				    "        {\n"
				    "          \"Kind\": 2,\n"
				    "          \"Flags\": 1,\n"
				    "          \"Data\": \"BEGIN\"\n"
				    "        },\n"
				    "        {\n"
				    "          \"Kind\": 1,\n"
				    "          \"Flags\": 0,\n"
				    "          \"Data\": \"aGVsbG8gd29ybGQA\"\n"
				    "        }\n"
				    "      ]\n"
				    "    }\n"
				    "  ]\n"
				    "}";

	/* check blob */
	blob2 = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_SHA256, data, FWUPD_JCAT_BLOB_FLAG_NONE);
	g_assert(fwupd_jcat_blob_get_data(blob2) == data); /* nocheck:blocked */
	blob1 = fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_GPG, "BEGIN");
	fwupd_jcat_blob_set_timestamp(blob1, 0);
	g_assert_cmpint(fwupd_jcat_blob_get_timestamp(blob1), ==, 0);
	fwupd_jcat_blob_set_timestamp(blob2, 0);
	g_assert_cmpint(fwupd_jcat_blob_get_timestamp(blob2), ==, 0);

	/* get default item */
	item4 = fwupd_jcat_file_get_item_default(file, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(item4);
	g_clear_error(&error);

	/* check item */
	g_assert_cmpstr(fwupd_jcat_item_get_id(item), ==, "firmware.bin");
	blobs0 = fwupd_jcat_item_get_blobs(item);
	g_assert_cmpint(blobs0->len, ==, 0);
	fwupd_jcat_item_add_blob(item, blob1);
	fwupd_jcat_item_add_blob(item, blob2);
	fwupd_jcat_item_add_alias_id(item, "foo.bin");
	blobs1 = fwupd_jcat_item_get_blobs(item);
	g_assert_cmpint(blobs1->len, ==, 2);
	blobs2 = fwupd_jcat_item_get_blobs_by_kind(item, FWUPD_JCAT_BLOB_KIND_GPG);
	g_assert_cmpint(blobs2->len, ==, 1);
	blobs3 = fwupd_jcat_item_get_blobs_by_kind(item, FWUPD_JCAT_BLOB_KIND_PKCS7);
	g_assert_cmpint(blobs3->len, ==, 0);

	/* check file */
	g_assert_cmpint(fwupd_jcat_file_get_version_major(file), ==, 0);
	g_assert_cmpint(fwupd_jcat_file_get_version_minor(file), ==, 1);
	items0 = fwupd_jcat_file_get_items(file);
	g_assert_cmpint(items0->len, ==, 0);
	fwupd_jcat_file_add_item(file, item);
	items1 = fwupd_jcat_file_get_items(file);
	g_assert_cmpint(items1->len, ==, 1);
	item1 = fwupd_jcat_file_get_item_by_id(file, "firmware.bin", &error);
	g_assert_no_error(error);
	g_assert_nonnull(item1);
	g_assert(item == item1);
	item2 = fwupd_jcat_file_get_item_by_id(file, "dave.bin", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(item2);
	g_clear_error(&error);

	/* get default item */
	item3 = fwupd_jcat_file_get_item_default(file, &error);
	g_assert_no_error(error);
	g_assert_nonnull(item3);

	/* export as string */
	json1 = fwupd_jcat_file_export_json(file, FWUPD_CODEC_FLAG_NO_TIMESTAMP, &error);
	g_debug("%s", json1);
	ret = fu_test_compare_lines(json1, json_perfect, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* export as compressed file */
	blob = fwupd_jcat_file_export_bytes(file, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* load compressed file */
	ret = fwupd_jcat_file_import_bytes(file2, blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	json2 = fwupd_jcat_file_export_json(file2, FWUPD_CODEC_FLAG_NO_TIMESTAMP, &error);
	g_debug("%s", json2);
	ret = fu_test_compare_lines(json1, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/jcat/blob", fwupd_jcat_blob_func);
	g_test_add_func("/fwupd/jcat/item", fwupd_jcat_item_func);
	g_test_add_func("/fwupd/jcat/file", fwupd_jcat_file_func);
	g_test_add_func("/fwupd/jcat/file/json", fwupd_jcat_file_json_func);
	return g_test_run();
}
