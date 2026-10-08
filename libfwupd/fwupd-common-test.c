/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "fwupd-client.h"
#include "fwupd-common-private.h"
#include "fwupd-device.h"
#include "fwupd-error.h"
#include "fwupd-release.h"
#include "fwupd-test.h"
#include "fwupd-variant.h"

static void
fwupd_common_checksum_func(void)
{
	g_autofree gchar *csum_md5 = g_strnfill(32, 'a');
	g_autofree gchar *csum_sha1 = g_strnfill(40, 'b');
	g_autofree gchar *csum_sha256 = g_strnfill(64, 'c');
	g_autofree gchar *csum_sha384 = g_strnfill(96, 'd');
	g_autofree gchar *csum_sha512 = g_strnfill(128, 'e');
	g_autofree gchar *fmt = NULL;
	g_autoptr(GPtrArray) checksums = g_ptr_array_new();

	/* guess the checksum kind from the string length */
	g_assert_cmpint(fwupd_checksum_guess_kind(csum_md5), ==, G_CHECKSUM_MD5);
	g_assert_cmpint(fwupd_checksum_guess_kind(csum_sha1), ==, G_CHECKSUM_SHA1);
	g_assert_cmpint(fwupd_checksum_guess_kind(csum_sha256), ==, G_CHECKSUM_SHA256);
	g_assert_cmpint(fwupd_checksum_guess_kind(csum_sha384), ==, G_CHECKSUM_SHA384);
	g_assert_cmpint(fwupd_checksum_guess_kind(csum_sha512), ==, G_CHECKSUM_SHA512);
	g_assert_cmpint(fwupd_checksum_guess_kind("short"), ==, G_CHECKSUM_SHA1);

	/* the display name for each kind */
	g_assert_cmpstr(fwupd_checksum_type_to_string_display(G_CHECKSUM_MD5), ==, "MD5");
	g_assert_cmpstr(fwupd_checksum_type_to_string_display(G_CHECKSUM_SHA1), ==, "SHA1");
	g_assert_cmpstr(fwupd_checksum_type_to_string_display(G_CHECKSUM_SHA256), ==, "SHA256");
	g_assert_cmpstr(fwupd_checksum_type_to_string_display(G_CHECKSUM_SHA384), ==, "SHA384");
	g_assert_cmpstr(fwupd_checksum_type_to_string_display(G_CHECKSUM_SHA512), ==, "SHA512");

	/* formatted for display */
	fmt = fwupd_checksum_format_for_display(csum_sha1);
	g_assert_cmpstr(fmt, ==, "SHA1(bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb)");

	/* lookups into an array of checksums */
	g_ptr_array_add(checksums, csum_sha1);
	g_ptr_array_add(checksums, csum_sha256);
	g_assert_cmpstr(fwupd_checksum_get_by_kind(checksums, G_CHECKSUM_SHA1), ==, csum_sha1);
	g_assert_cmpstr(fwupd_checksum_get_by_kind(checksums, G_CHECKSUM_SHA256), ==, csum_sha256);
	g_assert_null(fwupd_checksum_get_by_kind(checksums, G_CHECKSUM_MD5));

	/* SHA256 is preferred over SHA1 */
	g_assert_cmpstr(fwupd_checksum_get_best(checksums), ==, csum_sha256);
}

static void
fwupd_common_percentage_func(void)
{
	/* range checks */
	g_assert_true(fwupd_percentage_is_valid(0.0));
	g_assert_true(fwupd_percentage_is_valid(50.0));
	g_assert_true(fwupd_percentage_is_valid(100.0));
	g_assert_false(fwupd_percentage_is_valid(-1.0));
	g_assert_false(fwupd_percentage_is_valid(101.0));

	/* no significant change */
	g_assert_false(fwupd_percentage_delta_notify(50.0, 50.0));

	/* notify on a significant increase or decrease */
	g_assert_true(fwupd_percentage_delta_notify(50.0, 60.0));
	g_assert_true(fwupd_percentage_delta_notify(60.0, 50.0));

	/* notify when crossing 100%, even for a tiny change */
	g_assert_true(fwupd_percentage_delta_notify(99.999, 100.0));
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_common_unix_stream_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *tmpdir = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("hello", 5);
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) istream = NULL;
	g_autoptr(GInputStream) istream2 = NULL;
	g_autoptr(GInputStream) istream_fail = NULL;
	g_autoptr(GOutputStream) ostream = NULL;

	/* in-memory stream from a blob */
	istream = G_INPUT_STREAM(fwupd_unix_input_stream_from_bytes(blob, &error));
	g_assert_no_error(error);
	g_assert_nonnull(istream);
	blob2 = g_input_stream_read_bytes(istream, 5, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 5);
	g_assert_cmpint(memcmp(g_bytes_get_data(blob2, NULL), "hello", 5), ==, 0);

	/* write to a file, then read it back */
	tmpdir = g_dir_make_tmp("fwupd-common-self-test-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fn = g_build_filename(tmpdir, "stream.bin", NULL);
	ostream = G_OUTPUT_STREAM(fwupd_unix_output_stream_from_fn(fn, &error));
	g_assert_no_error(error);
	g_assert_nonnull(ostream);
	ret = g_output_stream_write_all(ostream, "hello", 5, NULL, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_output_stream_close(ostream, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	istream2 = G_INPUT_STREAM(fwupd_unix_input_stream_from_fn(fn, &error));
	g_assert_no_error(error);
	g_assert_nonnull(istream2);
	blob3 = g_input_stream_read_bytes(istream2, 5, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob3);
	g_assert_cmpint(g_bytes_get_size(blob3), ==, 5);

	/* a missing file is an error */
	istream_fail =
	    G_INPUT_STREAM(fwupd_unix_input_stream_from_fn("/this/does/not/exist.bin", &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(istream_fail);

	(void)g_unlink(fn);
	(void)g_rmdir(tmpdir);
}
#endif

static void
fwupd_variant_func(void)
{
	g_autoptr(GVariant) v_i32 = g_variant_new_int32(1234);
	g_autoptr(GVariant) v_i64 = g_variant_new_int64(1234);
	g_autoptr(GVariant) v_u32 = g_variant_new_uint32(1234);
	g_autoptr(GVariant) v_u64 = g_variant_new_uint64(1234);

	g_assert_cmpint(fwupd_variant_get_uint32(v_u32), ==, 1234);
	g_assert_cmpint(fwupd_variant_get_uint32(v_i32), ==, 1234);
	g_assert_cmpint(fwupd_variant_get_uint64(v_i64), ==, 1234);
	g_assert_cmpint(fwupd_variant_get_uint64(v_u64), ==, 1234);
}

static void
fwupd_common_history_report_func(void)
{
	gboolean ret;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdClient) client = fwupd_client_new();
	g_autoptr(FwupdDevice) dev = fwupd_device_new();
	g_autoptr(FwupdRelease) rel = fwupd_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) metadata = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GPtrArray) devs = g_ptr_array_new();

	fwupd_device_set_id(dev, "0000000000000000000000000000000000000000");
	fwupd_device_set_update_state(dev, FWUPD_UPDATE_STATE_FAILED);
	fwupd_device_add_checksum(dev, "beefdead");
	fwupd_device_add_guid(dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_device_add_protocol(dev, "org.hughski.colorhug");
	fwupd_device_set_plugin(dev, "hughski_colorhug");
	fwupd_device_set_update_error(dev, "device dead");
	fwupd_device_set_version(dev, "1.2.3");
	fwupd_release_add_checksum(rel, "beefdead");
	fwupd_release_set_id(rel, "123");
	fwupd_release_set_update_message(rel, "oops");
	fwupd_release_set_version(rel, "1.2.4");
	fwupd_device_add_release(dev, rel);

	/* metadata */
	g_hash_table_insert(metadata, (gpointer) "DistroId", (gpointer) "generic");
	g_hash_table_insert(metadata, (gpointer) "DistroVersion", (gpointer) "39");
	g_hash_table_insert(metadata, (gpointer) "DistroVariant", (gpointer) "workstation");

	g_ptr_array_add(devs, dev);
	json = fwupd_client_build_report_history(client, devs, NULL, metadata, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(json,
				    "{\n"
				    "  \"ReportType\": \"history\",\n"
				    "  \"ReportVersion\": 2,\n"
				    "  \"Metadata\": {\n"
				    "    \"DistroId\": \"generic\",\n"
				    "    \"DistroVariant\": \"workstation\",\n"
				    "    \"DistroVersion\": \"39\"\n"
				    "  },\n"
				    "  \"Reports\": [\n"
				    "    {\n"
				    "      \"Checksum\": \"beefdead\",\n"
				    "      \"ChecksumDevice\": [\n"
				    "        \"beefdead\"\n"
				    "      ],\n"
				    "      \"ReleaseId\": \"123\",\n"
				    "      \"UpdateState\": 3,\n"
				    "      \"UpdateError\": \"device dead\",\n"
				    "      \"UpdateMessage\": \"oops\",\n"
				    "      \"Guid\": [\n"
				    "        \"2082b5e0-7a64-478a-b1b2-e3404fab6dad\"\n"
				    "      ],\n"
				    "      \"Plugin\": \"hughski_colorhug\",\n"
				    "      \"VersionOld\": \"1.2.3\",\n"
				    "      \"VersionNew\": \"1.2.4\",\n"
				    "      \"Flags\": 0,\n"
				    "      \"Created\": 0,\n"
				    "      \"Modified\": 0\n"
				    "    }\n"
				    "  ]\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_common_device_id_func(void)
{
	g_assert_false(fwupd_device_id_is_valid(NULL));
	g_assert_false(fwupd_device_id_is_valid(""));
	g_assert_false(fwupd_device_id_is_valid("1ff60ab2-3905-06a1-b476-0371f00c9e9b"));
	g_assert_false(fwupd_device_id_is_valid("aaaaaad3fae86d95e5d56626129d00e332c4b8dac95442"));
	g_assert_false(fwupd_device_id_is_valid("x3fae86d95e5d56626129d00e332c4b8dac95442"));
	g_assert_false(fwupd_device_id_is_valid("D3FAE86D95E5D56626129D00E332C4B8DAC95442"));
	g_assert_false(fwupd_device_id_is_valid(FWUPD_DEVICE_ID_ANY));
	g_assert_true(fwupd_device_id_is_valid("d3fae86d95e5d56626129d00e332c4b8dac95442"));
}

static void
fwupd_common_guid_func(void)
{
	const guint8 msbuf[] = "hello world!";
	g_autofree gchar *guid1 = NULL;
	g_autofree gchar *guid2 = NULL;
	g_autofree gchar *guid3 = NULL;
	g_autofree gchar *guid_be = NULL;
	g_autofree gchar *guid_me = NULL;
	fwupd_guid_t buf = {0x0};
	gboolean ret;
	g_autoptr(GError) error = NULL;

	/* invalid */
	g_assert_false(fwupd_guid_is_valid(NULL));
	g_assert_false(fwupd_guid_is_valid(""));
	g_assert_false(fwupd_guid_is_valid("1ff60ab2-3905-06a1-b476"));
	g_assert_false(fwupd_guid_is_valid("1ff60ab2-XXXX-XXXX-XXXX-0371f00c9e9b"));
	g_assert_false(fwupd_guid_is_valid("1ff60ab2-XXXX-XXXX-XXXX-0371f00c9e9bf"));
	g_assert_false(fwupd_guid_is_valid(" 1ff60ab2-3905-06a1-b476-0371f00c9e9b"));
	g_assert_false(fwupd_guid_is_valid("00000000-0000-0000-0000-000000000000"));

	/* valid */
	g_assert_true(fwupd_guid_is_valid("1ff60ab2-3905-06a1-b476-0371f00c9e9b"));

	/* make valid */
	guid1 = fwupd_guid_hash_string("python.org");
	g_assert_cmpstr(guid1, ==, "886313e1-3b8a-5372-9b90-0c9aee199e5d");

	guid2 = fwupd_guid_hash_string("8086:0406");
	g_assert_cmpstr(guid2, ==, "1fbd1f2c-80f4-5d7c-a6ad-35c7b9bd5486");

	guid3 = fwupd_guid_hash_data(msbuf, sizeof(msbuf), FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT);
	g_assert_cmpstr(guid3, ==, "6836cfac-f77a-527f-b375-4f92f01449c5");

	/* round-trip BE */
	ret = fwupd_guid_from_string("00112233-4455-6677-8899-aabbccddeeff",
				     &buf,
				     FWUPD_GUID_FLAG_NONE,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(memcmp(buf,
			       "\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff",
			       sizeof(buf)),
			==,
			0);
	guid_be = fwupd_guid_to_string((const fwupd_guid_t *)&buf, FWUPD_GUID_FLAG_NONE);
	g_assert_cmpstr(guid_be, ==, "00112233-4455-6677-8899-aabbccddeeff");

	/* round-trip mixed encoding */
	ret = fwupd_guid_from_string("00112233-4455-6677-8899-aabbccddeeff",
				     &buf,
				     FWUPD_GUID_FLAG_MIXED_ENDIAN,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(memcmp(buf,
			       "\x33\x22\x11\x00\x55\x44\x77\x66\x88\x99\xaa\xbb\xcc\xdd\xee\xff",
			       sizeof(buf)),
			==,
			0);
	guid_me = fwupd_guid_to_string((const fwupd_guid_t *)&buf, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	g_assert_cmpstr(guid_me, ==, "00112233-4455-6677-8899-aabbccddeeff");

	/* check failure */
	g_assert_false(
	    fwupd_guid_from_string("001122334455-6677-8899-aabbccddeeff", NULL, 0, NULL));
	g_assert_false(
	    fwupd_guid_from_string("0112233-4455-6677-8899-aabbccddeeff", NULL, 0, NULL));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/common/checksum", fwupd_common_checksum_func);
	g_test_add_func("/fwupd/common/device-id", fwupd_common_device_id_func);
	g_test_add_func("/fwupd/common/guid", fwupd_common_guid_func);
	g_test_add_func("/fwupd/common/history-report", fwupd_common_history_report_func);
	g_test_add_func("/fwupd/common/percentage", fwupd_common_percentage_func);
#ifdef HAVE_GIO_UNIX
	g_test_add_func("/fwupd/common/unix-stream", fwupd_common_unix_stream_func);
#endif
	g_test_add_func("/fwupd/common/variant", fwupd_variant_func);
	return g_test_run();
}
