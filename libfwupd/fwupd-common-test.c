/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-client.h"
#include "fwupd-common.h"
#include "fwupd-device.h"
#include "fwupd-release.h"
#include "fwupd-test.h"

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
	g_test_add_func("/fwupd/common/device-id", fwupd_common_device_id_func);
	g_test_add_func("/fwupd/common/guid", fwupd_common_guid_func);
	g_test_add_func("/fwupd/common/history-report", fwupd_common_history_report_func);
	return g_test_run();
}
