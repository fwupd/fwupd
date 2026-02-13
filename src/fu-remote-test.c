/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-remote.h"
#include "fu-test.h"

static void
fu_remote_download_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *expected_metadata = NULL;
	g_autofree gchar *expected_signature = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes2.d", NULL);
	expected_metadata = g_build_filename(FWUPD_LOCALSTATEDIR,
					     "lib",
					     "fwupd",
					     "remotes2.d",
					     "lvfs-testing",
					     "firmware.xml.gz",
					     NULL);
	expected_signature = g_strdup_printf("%s.jcat", expected_metadata);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "remotes2.d", "lvfs-testing.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_nonnull(fwupd_remote_get_metadata_uri(remote));
	g_assert_nonnull(fwupd_remote_get_metadata_uri_sig(remote));
	g_assert_cmpstr(fwupd_remote_get_title(remote),
			==,
			"Linux Vendor Firmware Service (testing)");
	g_assert_cmpstr(fwupd_remote_get_report_uri(remote),
			==,
			"https://fwupd.org/lvfs/firmware/report");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote), ==, expected_metadata);
	g_assert_cmpstr(fwupd_remote_get_filename_cache_sig(remote), ==, expected_signature);
}

/* verify we used the FirmwareBaseURI just for firmware */
static void
fu_remote_baseuri_func(void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autofree gchar *directory = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes2.d", NULL);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "firmware-base-uri.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_cmpstr(fwupd_remote_get_firmware_base_uri(remote), ==, "https://my.fancy.cdn/");
	g_assert_cmpstr(fwupd_remote_get_agreement(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_checksum(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote),
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz");
	g_assert_cmpstr(fwupd_remote_get_metadata_uri_sig(remote),
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz.jcat");
	firmware_uri =
	    fwupd_remote_build_firmware_uri(remote, "http://bbc.co.uk/firmware.cab", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(firmware_uri, ==, "https://my.fancy.cdn/firmware.cab");
}

static void
fu_remote_auth_func(void)
{
	gboolean ret;
	gchar **order;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *remotes_dir = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FwupdRemote) remote2 = fwupd_remote_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	remotes_dir = g_test_build_filename(G_TEST_BUILT, "tests", NULL);
	fwupd_remote_set_remotes_dir(remote, remotes_dir);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "auth.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_remote_get_username(remote), ==, "user");
	g_assert_cmpstr(fwupd_remote_get_password(remote), ==, "pass");
	g_assert_cmpstr(fwupd_remote_get_report_uri(remote),
			==,
			"https://fwupd.org/lvfs/firmware/report");
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED));
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS));
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS));

	g_assert_true(
	    g_str_has_suffix(fwupd_remote_get_filename_source(remote), "tests/auth.conf"));
	g_assert_true(g_str_has_suffix(fwupd_remote_get_remotes_dir(remote), "/src/tests"));
	g_assert_cmpint(fwupd_remote_get_age(remote), >, 1000000);

	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	order = fwupd_remote_get_order_before(remote);
	g_assert_nonnull(order);
	g_assert_cmpint(g_strv_length(order), ==, 1);
	g_assert_cmpstr(order[0], ==, "before");
	order = fwupd_remote_get_order_after(remote);
	g_assert_nonnull(order);
	g_assert_cmpint(g_strv_length(order), ==, 1);
	g_assert_cmpstr(order[0], ==, "after");

	/* to/from GVariant */
	fwupd_remote_set_priority(remote, 999);
	data = fwupd_codec_to_variant(FWUPD_CODEC(remote), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(remote2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_remote_get_username(remote2), ==, "user");
	g_assert_cmpint(fwupd_remote_get_priority(remote2), ==, 999);

	/* jcat-tool is not a hard dep, and the tests create an empty file if unfound */
	ret = fwupd_remote_load_signature(remote,
					  fwupd_remote_get_filename_cache_sig(remote),
					  &error);
	if (!ret) {
		if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_test_skip("no jcat-tool, so skipping test");
			return;
		}
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to JSON */
	fwupd_remote_set_filename_source(remote2, NULL);
	fwupd_remote_set_checksum_sig(
	    remote2,
	    "dd1b4fd2a59bb0e4d9ea760c658ac3cf9336c7b6729357bab443485b5cf071b2");
	fwupd_remote_set_filename_cache(remote2, "./libfwupd/tests/auth/firmware.xml.gz");
	json = fwupd_codec_to_json_string(FWUPD_CODEC(remote2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret =
	    g_strcmp0(
		json,
		"{\n"
		"  \"Id\": \"auth\",\n"
		"  \"Kind\": \"download\",\n"
		"  \"ReportUri\": \"https://fwupd.org/lvfs/firmware/report\",\n"
		"  \"MetadataUri\": \"https://cdn.fwupd.org/downloads/firmware.xml.gz\",\n"
		"  \"MetadataUriSig\": \"https://cdn.fwupd.org/downloads/firmware.xml.gz.jcat\",\n"
		"  \"FirmwareBaseUri\": \"https://my.fancy.cdn/\",\n"
		"  \"Username\": \"user\",\n"
		"  \"Password\": \"pass\",\n"
		"  \"ChecksumSig\": "
		"\"dd1b4fd2a59bb0e4d9ea760c658ac3cf9336c7b6729357bab443485b5cf071b2\",\n"
		"  \"FilenameCache\": \"./libfwupd/tests/auth/firmware.xml.gz\",\n"
		"  \"FilenameCacheSig\": \"./libfwupd/tests/auth/firmware.xml.gz.jcat\",\n"
		"  \"Flags\": 9,\n"
		"  \"Enabled\": true,\n"
		"  \"ApprovalRequired\": false,\n"
		"  \"AutomaticReports\": false,\n"
		"  \"AutomaticSecurityReports\": true,\n"
		"  \"Priority\": 999,\n"
		"  \"Mtime\": 0,\n"
		"  \"RefreshInterval\": 86400\n"
		"}") == 0;
	g_assert_true(ret);
}

static void
fu_remote_duplicate_func(void)
{
	gboolean ret;
	g_autofree gchar *fn2 = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "stable.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fn2 = g_test_build_filename(G_TEST_DIST, "tests", "disabled.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn2, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_cmpstr(fwupd_remote_get_username(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_password(remote), ==, "");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote),
			==,
			"/tmp/fwupd-self-test/stable.xml");
}

/* verify we used the metadata path for firmware */
static void
fu_remote_nopath_func(void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *directory = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes2.d", NULL);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "firmware-nopath.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_cmpstr(fwupd_remote_get_checksum(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote),
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz");
	g_assert_cmpstr(fwupd_remote_get_metadata_uri_sig(remote),
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz.jcat");
	firmware_uri = fwupd_remote_build_firmware_uri(remote, "firmware.cab", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(firmware_uri,
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.cab");
}

static void
fu_remote_local_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(FwupdRemote) remote2 = fwupd_remote_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	remote = fwupd_remote_new();
	fn = g_test_build_filename(G_TEST_DIST, "tests", "dell-esrt.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_LOCAL);
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_null(fwupd_remote_get_metadata_uri(remote));
	g_assert_null(fwupd_remote_get_metadata_uri_sig(remote));
	g_assert_null(fwupd_remote_get_report_uri(remote));
	g_assert_cmpstr(fwupd_remote_get_title(remote),
			==,
			"Enable UEFI capsule updates on Dell systems");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote),
			==,
			"@datadir@/fwupd/remotes.d/dell-esrt/firmware.xml");
	g_assert_cmpstr(fwupd_remote_get_filename_cache_sig(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_checksum(remote), ==, NULL);

	/* to/from GVariant */
	data = fwupd_codec_to_variant(FWUPD_CODEC(remote), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(remote2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_null(fwupd_remote_get_metadata_uri(remote));

	/* to JSON */
	fwupd_remote_set_filename_source(remote2, NULL);
	json = fwupd_codec_to_json_string(FWUPD_CODEC(remote2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(
	    json,
	    "{\n"
	    "  \"Id\": \"dell-esrt\",\n"
	    "  \"Kind\": \"local\",\n"
	    "  \"Title\": \"Enable UEFI capsule updates on Dell systems\",\n"
	    "  \"FilenameCache\": \"@datadir@/fwupd/remotes.d/dell-esrt/firmware.xml\",\n"
	    "  \"Flags\": 1,\n"
	    "  \"Enabled\": true,\n"
	    "  \"ApprovalRequired\": false,\n"
	    "  \"AutomaticReports\": false,\n"
	    "  \"AutomaticSecurityReports\": false,\n"
	    "  \"Priority\": 0,\n"
	    "  \"Mtime\": 0,\n"
	    "  \"RefreshInterval\": 0\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/remote/download", fu_remote_download_func);
	g_test_add_func("/fwupd/remote/base-uri", fu_remote_baseuri_func);
	g_test_add_func("/fwupd/remote/no-path", fu_remote_nopath_func);
	g_test_add_func("/fwupd/remote/local", fu_remote_local_func);
	g_test_add_func("/fwupd/remote/duplicate", fu_remote_duplicate_func);
	g_test_add_func("/fwupd/remote/auth", fu_remote_auth_func);
	return g_test_run();
}
