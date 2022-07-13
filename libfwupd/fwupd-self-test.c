/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <locale.h>
#include <string.h>
#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif

#include "fwupd-client-sync.h"
#include "fwupd-client.h"
#include "fwupd-common.h"
#include "fwupd-device-private.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-plugin-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-request-private.h"
#include "fwupd-security-attr-private.h"

static gboolean
fu_test_compare_lines(const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;

	/* exactly the same */
	if (g_strcmp0(txt1, txt2) == 0)
		return TRUE;

		/* matches a pattern */
#ifdef HAVE_FNMATCH_H
	if (fnmatch(txt2, txt1, FNM_NOESCAPE) == 0)
		return TRUE;
#else
	if (g_strcmp0(txt1, txt2) == 0)
		return TRUE;
#endif

	/* save temp files and diff them */
	if (!g_file_set_contents("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync("diff -urNp /tmp/b /tmp/a", &output, NULL, NULL, error))
		return FALSE;

	/* just output the diff */
	g_set_error_literal(error, 1, 0, output);
	return FALSE;
}

/* https://gitlab.gnome.org/GNOME/glib/issues/225 */
static guint
_g_string_replace(GString *string, const gchar *search, const gchar *replace)
{
	gchar *tmp;
	guint count = 0;
	gsize search_idx = 0;
	gsize replace_len;
	gsize search_len;

	g_return_val_if_fail(string != NULL, 0);
	g_return_val_if_fail(search != NULL, 0);
	g_return_val_if_fail(replace != NULL, 0);

	/* nothing to do */
	if (string->len == 0)
		return 0;

	search_len = strlen(search);
	replace_len = strlen(replace);

	do {
		tmp = g_strstr_len(string->str + search_idx, -1, search);
		if (tmp == NULL)
			break;

		/* advance the counter in case @replace contains @search */
		search_idx = (gsize)(tmp - string->str);

		/* reallocate the string if required */
		if (search_len > replace_len) {
			g_string_erase(string,
				       (gssize)search_idx,
				       (gssize)(search_len - replace_len));
			memcpy(tmp, replace, replace_len);
		} else if (search_len < replace_len) {
			g_string_insert_len(string,
					    (gssize)search_idx,
					    replace,
					    (gssize)(replace_len - search_len));
			/* we have to treat this specially as it could have
			 * been reallocated when the insertion happened */
			memcpy(string->str + search_idx, replace, replace_len);
		} else {
			/* just memcmp in the new string */
			memcpy(tmp, replace, replace_len);
		}
		search_idx += replace_len;
		count++;
	} while (TRUE);

	return count;
}

static void
fwupd_enums_func(void)
{
	/* enums */
	for (guint i = 0; i < FWUPD_ERROR_LAST; i++) {
		const gchar *tmp = fwupd_error_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_error_from_string(tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_STATUS_LAST; i++) {
		const gchar *tmp = fwupd_status_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_status_from_string(tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_UPDATE_STATE_LAST; i++) {
		const gchar *tmp = fwupd_update_state_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_update_state_from_string(tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_TRUST_FLAG_LAST; i++) {
		const gchar *tmp = fwupd_trust_flag_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_trust_flag_from_string(tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_REQUEST_KIND_LAST; i++) {
		const gchar *tmp = fwupd_request_kind_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_request_kind_from_string(tmp), ==, i);
	}
	for (guint i = FWUPD_RELEASE_URGENCY_UNKNOWN + 1; i < FWUPD_RELEASE_URGENCY_LAST; i++) {
		const gchar *tmp = fwupd_release_urgency_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_release_urgency_from_string(tmp), ==, i);
	}
	for (guint i = 1; i < FWUPD_VERSION_FORMAT_LAST; i++) {
		const gchar *tmp = fwupd_version_format_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_version_format_from_string(tmp), ==, i);
	}
	for (guint i = 1; i < FWUPD_REMOTE_KIND_LAST; i++) {
		const gchar *tmp = fwupd_remote_kind_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_remote_kind_from_string(tmp), ==, i);
	}

	/* bitfield */
	for (guint64 i = 1; i <= FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD; i *= 2) {
		const gchar *tmp = fwupd_device_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing device flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_device_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED; i *= 2) {
		const gchar *tmp = fwupd_device_problem_to_string(i);
		if (tmp == NULL)
			g_warning("missing device problem 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_device_problem_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_PLUGIN_FLAG_AUTH_REQUIRED; i *= 2) {
		const gchar *tmp = fwupd_plugin_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing plugin flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_plugin_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_FEATURE_FLAG_SHOW_PROBLEMS; i *= 2) {
		const gchar *tmp = fwupd_feature_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing feature flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_feature_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_RELEASE_FLAG_IS_COMMUNITY; i *= 2) {
		const gchar *tmp = fwupd_release_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing release flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_release_flag_from_string(tmp), ==, i);
	}
	for (guint i = 1; i < FWUPD_KEYRING_KIND_LAST; i++) {
		const gchar *tmp = fwupd_keyring_kind_to_string(i);
		if (tmp == NULL)
			g_warning("missing keyring kind 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_keyring_kind_from_string(tmp), ==, i);
	}
}

static void
fwupd_remote_download_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *expected_metadata = NULL;
	g_autofree gchar *expected_signature = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes.d", NULL);
	expected_metadata = g_build_filename(FWUPD_LOCALSTATEDIR,
					     "lib",
					     "fwupd",
					     "remotes.d",
					     "lvfs-testing",
					     "metadata.xml.gz",
					     NULL);
	expected_signature = g_strdup_printf("%s.jcat", expected_metadata);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "remotes.d", "lvfs-testing.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_keyring_kind(remote), ==, FWUPD_KEYRING_KIND_JCAT);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_false(fwupd_remote_get_enabled(remote));
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
fwupd_remote_baseuri_func(void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autofree gchar *directory = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes.d", NULL);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "firmware-base-uri.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_keyring_kind(remote), ==, FWUPD_KEYRING_KIND_JCAT);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_true(fwupd_remote_get_enabled(remote));
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

static gchar *
fwupd_remote_to_json_string(FwupdRemote *remote, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;
	json_builder_begin_object(builder);
	fwupd_remote_to_json(remote, builder);
	json_builder_end_object(builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to convert remote to json.");
		return NULL;
	}
	return g_steal_pointer(&data);
}

static void
fwupd_remote_auth_func(void)
{
	gboolean ret;
	gchar **order;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *remotes_dir = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	remotes_dir = g_test_build_filename(G_TEST_BUILT, "tests", NULL);
	fwupd_remote_set_remotes_dir(remote, remotes_dir);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "auth.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_remote_get_username(remote), ==, "user");
	g_assert_cmpstr(fwupd_remote_get_password(remote), ==, "pass");
	g_assert_cmpstr(fwupd_remote_get_security_report_uri(remote),
			==,
			"https://fwupd.org/lvfs/hsireports/upload");
	g_assert_false(fwupd_remote_get_approval_required(remote));
	g_assert_false(fwupd_remote_get_automatic_reports(remote));
	g_assert_true(fwupd_remote_get_automatic_security_reports(remote));

	g_assert_true(
	    g_str_has_suffix(fwupd_remote_get_filename_source(remote), "tests/auth.conf"));
	g_assert_true(g_str_has_suffix(fwupd_remote_get_remotes_dir(remote), "/libfwupd/tests"));
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
	data = fwupd_remote_to_variant(remote);
	remote2 = fwupd_remote_from_variant(data);
	g_assert_cmpstr(fwupd_remote_get_username(remote2), ==, "user");
	g_assert_cmpint(fwupd_remote_get_priority(remote2), ==, 999);

	/* jcat-tool is not a hard dep, and the tests create an empty file if unfound */
	ret = fwupd_remote_load_signature(remote,
					  fwupd_remote_get_filename_cache_sig(remote),
					  &error);
	if (!ret) {
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT)) {
			g_test_skip("no jcat-tool, so skipping test");
			return;
		}
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to JSON */
	fwupd_remote_set_filename_source(remote2, NULL);
	fwupd_remote_set_checksum(
	    remote2,
	    "dd1b4fd2a59bb0e4d9ea760c658ac3cf9336c7b6729357bab443485b5cf071b2");
	fwupd_remote_set_filename_cache(remote2, "./libfwupd/tests/auth/metadata.xml.gz");
	json = fwupd_remote_to_json_string(remote2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(
	    json,
	    "{\n"
	    "  \"Id\" : \"auth\",\n"
	    "  \"Kind\" : \"download\",\n"
	    "  \"KeyringKind\" : \"jcat\",\n"
	    "  \"FirmwareBaseUri\" : \"https://my.fancy.cdn/\",\n"
	    "  \"ReportUri\" : \"https://fwupd.org/lvfs/firmware/report\",\n"
	    "  \"SecurityReportUri\" : \"https://fwupd.org/lvfs/hsireports/upload\",\n"
	    "  \"MetadataUri\" : \"https://cdn.fwupd.org/downloads/firmware.xml.gz\",\n"
	    "  \"MetadataUriSig\" : \"https://cdn.fwupd.org/downloads/firmware.xml.gz.jcat\",\n"
	    "  \"Username\" : \"user\",\n"
	    "  \"Password\" : \"pass\",\n"
	    "  \"Checksum\" : "
	    "\"dd1b4fd2a59bb0e4d9ea760c658ac3cf9336c7b6729357bab443485b5cf071b2\",\n"
	    "  \"FilenameCache\" : \"./libfwupd/tests/auth/metadata.xml.gz\",\n"
	    "  \"FilenameCacheSig\" : \"./libfwupd/tests/auth/metadata.xml.gz.jcat\",\n"
	    "  \"Enabled\" : \"true\",\n"
	    "  \"ApprovalRequired\" : \"false\",\n"
	    "  \"AutomaticReports\" : \"false\",\n"
	    "  \"AutomaticSecurityReports\" : \"true\",\n"
	    "  \"Priority\" : 999,\n"
	    "  \"Mtime\" : 0\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_remote_duplicate_func(void)
{
	gboolean ret;
	g_autofree gchar *fn2 = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "stable.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fn2 = g_test_build_filename(G_TEST_DIST, "tests", "disabled.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn2, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fwupd_remote_get_enabled(remote));
	g_assert_cmpint(fwupd_remote_get_keyring_kind(remote), ==, FWUPD_KEYRING_KIND_NONE);
	g_assert_cmpstr(fwupd_remote_get_username(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_password(remote), ==, "");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote),
			==,
			"/tmp/fwupd-self-test/stable.xml");
}

/* verify we used the metadata path for firmware */
static void
fwupd_remote_nopath_func(void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *directory = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes.d", NULL);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "firmware-nopath.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_keyring_kind(remote), ==, FWUPD_KEYRING_KIND_JCAT);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_true(fwupd_remote_get_enabled(remote));
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
fwupd_remote_local_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	remote = fwupd_remote_new();
	fn = g_test_build_filename(G_TEST_DIST, "tests", "dell-esrt.conf", NULL);
	ret = fwupd_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_LOCAL);
	g_assert_cmpint(fwupd_remote_get_keyring_kind(remote), ==, FWUPD_KEYRING_KIND_NONE);
	g_assert_true(fwupd_remote_get_enabled(remote));
	g_assert_null(fwupd_remote_get_metadata_uri(remote));
	g_assert_null(fwupd_remote_get_metadata_uri_sig(remote));
	g_assert_null(fwupd_remote_get_report_uri(remote));
	g_assert_cmpstr(fwupd_remote_get_title(remote),
			==,
			"Enable UEFI capsule updates on Dell systems");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote),
			==,
			"@datadir@/fwupd/remotes.d/dell-esrt/metadata.xml");
	g_assert_cmpstr(fwupd_remote_get_filename_cache_sig(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_checksum(remote), ==, NULL);

	/* to/from GVariant */
	data = fwupd_remote_to_variant(remote);
	remote2 = fwupd_remote_from_variant(data);
	g_assert_null(fwupd_remote_get_metadata_uri(remote));

	/* to JSON */
	fwupd_remote_set_filename_source(remote2, NULL);
	json = fwupd_remote_to_json_string(remote2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(
	    json,
	    "{\n"
	    "  \"Id\" : \"dell-esrt\",\n"
	    "  \"Kind\" : \"local\",\n"
	    "  \"KeyringKind\" : \"none\",\n"
	    "  \"Title\" : \"Enable UEFI capsule updates on Dell systems\",\n"
	    "  \"FilenameCache\" : \"@datadir@/fwupd/remotes.d/dell-esrt/metadata.xml\",\n"
	    "  \"Enabled\" : \"true\",\n"
	    "  \"ApprovalRequired\" : \"false\",\n"
	    "  \"AutomaticReports\" : \"false\",\n"
	    "  \"AutomaticSecurityReports\" : \"false\",\n"
	    "  \"Priority\" : 0,\n"
	    "  \"Mtime\" : 0\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static gchar *
fwupd_release_to_json_string(FwupdRelease *release, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;
	json_builder_begin_object(builder);
	fwupd_release_to_json(release, builder);
	json_builder_end_object(builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to convert release to json.");
		return NULL;
	}
	return g_steal_pointer(&data);
}

static void
fwupd_release_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdRelease) release1 = NULL;
	g_autoptr(FwupdRelease) release2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	release1 = fwupd_release_new();
	fwupd_release_add_metadata_item(release1, "foo", "bar");
	fwupd_release_add_metadata_item(release1, "baz", "bam");
	fwupd_release_set_remote_id(release1, "remote-id");
	fwupd_release_set_appstream_id(release1, "appstream-id");
	fwupd_release_set_id(release1, "id");
	fwupd_release_set_detach_caption(release1, "detach_caption");
	fwupd_release_set_detach_image(release1, "detach_image");
	fwupd_release_set_update_message(release1, "update_message");
	fwupd_release_set_update_image(release1, "update_image");
	fwupd_release_set_filename(release1, "filename");
	fwupd_release_set_protocol(release1, "protocol");
	fwupd_release_set_license(release1, "license");
	fwupd_release_set_name(release1, "name");
	fwupd_release_set_name_variant_suffix(release1, "name_variant_suffix");
	fwupd_release_set_summary(release1, "summary");
	fwupd_release_set_branch(release1, "branch");
	fwupd_release_set_description(release1, "description");
	fwupd_release_set_homepage(release1, "homepage");
	fwupd_release_set_details_url(release1, "details_url");
	fwupd_release_set_source_url(release1, "source_url");
	fwupd_release_set_version(release1, "version");
	fwupd_release_set_vendor(release1, "vendor");
	fwupd_release_set_size(release1, 1234);
	fwupd_release_set_created(release1, 5678);
	fwupd_release_set_install_duration(release1, 2468);
	fwupd_release_add_category(release1, "category");
	fwupd_release_add_category(release1, "category");
	fwupd_release_add_issue(release1, "issue");
	fwupd_release_add_issue(release1, "issue");
	fwupd_release_add_location(release1, "location");
	fwupd_release_add_location(release1, "location");
	fwupd_release_add_tag(release1, "tag");
	fwupd_release_add_tag(release1, "tag");
	fwupd_release_add_checksum(release1, "checksum");
	fwupd_release_add_checksum(release1, "checksum");
	fwupd_release_add_flag(release1, FWUPD_RELEASE_FLAG_IS_UPGRADE);
	fwupd_release_add_flag(release1, FWUPD_RELEASE_FLAG_IS_UPGRADE);
	fwupd_release_add_flag(release1, FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL);
	fwupd_release_remove_flag(release1, FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL);
	fwupd_release_set_urgency(release1, FWUPD_RELEASE_URGENCY_MEDIUM);
	data = fwupd_release_to_variant(release1);
	release2 = fwupd_release_from_variant(data);
	g_assert_cmpstr(fwupd_release_get_metadata_item(release2, "foo"), ==, "bar");
	g_assert_cmpstr(fwupd_release_get_metadata_item(release2, "baz"), ==, "bam");
	g_assert_cmpstr(fwupd_release_get_remote_id(release2), ==, "remote-id");
	g_assert_cmpstr(fwupd_release_get_appstream_id(release2), ==, "appstream-id");
	g_assert_cmpstr(fwupd_release_get_id(release2), ==, "id");
	g_assert_cmpstr(fwupd_release_get_detach_caption(release2), ==, "detach_caption");
	g_assert_cmpstr(fwupd_release_get_detach_image(release2), ==, "detach_image");
	g_assert_cmpstr(fwupd_release_get_update_message(release2), ==, "update_message");
	g_assert_cmpstr(fwupd_release_get_update_image(release2), ==, "update_image");
	g_assert_cmpstr(fwupd_release_get_filename(release2), ==, "filename");
	g_assert_cmpstr(fwupd_release_get_protocol(release2), ==, "protocol");
	g_assert_cmpstr(fwupd_release_get_license(release2), ==, "license");
	g_assert_cmpstr(fwupd_release_get_name(release2), ==, "name");
	g_assert_cmpstr(fwupd_release_get_name_variant_suffix(release2), ==, "name_variant_suffix");
	g_assert_cmpstr(fwupd_release_get_summary(release2), ==, "summary");
	g_assert_cmpstr(fwupd_release_get_branch(release2), ==, "branch");
	g_assert_cmpstr(fwupd_release_get_description(release2), ==, "description");
	g_assert_cmpstr(fwupd_release_get_homepage(release2), ==, "homepage");
	g_assert_cmpstr(fwupd_release_get_details_url(release2), ==, "details_url");
	g_assert_cmpstr(fwupd_release_get_source_url(release2), ==, "source_url");
	g_assert_cmpstr(fwupd_release_get_version(release2), ==, "version");
	g_assert_cmpstr(fwupd_release_get_vendor(release2), ==, "vendor");
	g_assert_cmpint(fwupd_release_get_size(release2), ==, 1234);
	g_assert_cmpint(fwupd_release_get_created(release2), ==, 5678);
	g_assert_true(fwupd_release_has_category(release2, "category"));
	g_assert_true(fwupd_release_has_tag(release2, "tag"));
	g_assert_true(fwupd_release_has_checksum(release2, "checksum"));
	g_assert_true(fwupd_release_has_flag(release2, FWUPD_RELEASE_FLAG_IS_UPGRADE));
	g_assert_false(fwupd_release_has_flag(release2, FWUPD_RELEASE_FLAG_IS_COMMUNITY));
	g_assert_cmpint(fwupd_release_get_issues(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_locations(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_categories(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_tags(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_checksums(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_urgency(release2), ==, FWUPD_RELEASE_URGENCY_MEDIUM);
	g_assert_cmpint(fwupd_release_get_install_duration(release2), ==, 2468);

	/* to JSON */
	json1 = fwupd_release_to_json_string(release1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	json2 = fwupd_release_to_json_string(release2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = fu_test_compare_lines(json1, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to string */
	str = fwupd_release_to_string(release2);
	ret = fu_test_compare_lines(str,
				    "  AppstreamId:          appstream-id\n"
				    "  ReleaseId:            id\n"
				    "  RemoteId:             remote-id\n"
				    "  Summary:              summary\n"
				    "  Description:          description\n"
				    "  Branch:               branch\n"
				    "  Version:              version\n"
				    "  Filename:             filename\n"
				    "  Protocol:             protocol\n"
				    "  Categories:           category\n"
				    "  Issues:               issue\n"
				    "  Checksum:             SHA1(checksum)\n"
				    "  Tags:                 tag\n"
				    "  License:              license\n"
				    "  Size:                 1.2 kB\n"
				    "  Created:              1970-01-01\n"
				    "  Uri:                  location\n"
				    "  Homepage:             homepage\n"
				    "  DetailsUrl:           details_url\n"
				    "  SourceUrl:            source_url\n"
				    "  Urgency:              medium\n"
				    "  Vendor:               vendor\n"
				    "  Flags:                is-upgrade\n"
				    "  InstallDuration:      2468\n"
				    "  DetachCaption:        detach_caption\n"
				    "  DetachImage:          detach_image\n"
				    "  UpdateMessage:        update_message\n"
				    "  UpdateImage:          update_image\n"
				    "  foo:                  bar\n"
				    "  baz:                  bam\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_plugin_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdPlugin) plugin1 = NULL;
	g_autoptr(FwupdPlugin) plugin2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	plugin1 = fwupd_plugin_new();
	fwupd_plugin_set_name(plugin1, "foo");
	fwupd_plugin_set_flags(plugin1, FWUPD_PLUGIN_FLAG_USER_WARNING);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	fwupd_plugin_remove_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	fwupd_plugin_remove_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	data = fwupd_plugin_to_variant(plugin1);
	plugin2 = fwupd_plugin_from_variant(data);
	g_assert_cmpstr(fwupd_plugin_get_name(plugin2), ==, "foo");
	g_assert_cmpint(fwupd_plugin_get_flags(plugin2),
			==,
			FWUPD_PLUGIN_FLAG_USER_WARNING | FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	g_assert_true(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_USER_WARNING));
	g_assert_true(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE));
	g_assert_false(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_NO_HARDWARE));

	str = fwupd_plugin_to_string(plugin2);
	ret = fu_test_compare_lines(str,
				    "  Name:                 foo\n"
				    "  Flags:                user-warning|clear-updatable\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_request_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	g_autoptr(FwupdRequest) request2 = NULL;
	g_autoptr(GVariant) data = NULL;

	/* create dummy */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_set_message(request, "foo");
	fwupd_request_set_image(request, "bar");
	fwupd_request_set_device_id(request, "950da62d4c753a26e64f7f7d687104ce38e32ca5");
	str = fwupd_request_to_string(request);
	g_debug("%s", str);

	/* set in init */
	g_assert_cmpint(fwupd_request_get_created(request), >, 0);

	/* to serialized and back again */
	data = fwupd_request_to_variant(request);
	request2 = fwupd_request_from_variant(data);
	g_assert_cmpint(fwupd_request_get_kind(request2), ==, FWUPD_REQUEST_KIND_IMMEDIATE);
	g_assert_cmpint(fwupd_request_get_created(request2), >, 0);
	g_assert_cmpstr(fwupd_request_get_id(request2), ==, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	g_assert_cmpstr(fwupd_request_get_message(request2), ==, "foo");
	g_assert_cmpstr(fwupd_request_get_image(request2), ==, "bar");
	g_assert_cmpstr(fwupd_request_get_device_id(request2),
			==,
			"950da62d4c753a26e64f7f7d687104ce38e32ca5");
}

static void
fwupd_device_func(void)
{
	gboolean ret;
	g_autofree gchar *data = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdDevice) dev_new = fwupd_device_new();
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str_ascii = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* create dummy object */
	dev = fwupd_device_new();
	fwupd_device_add_checksum(dev, "beefdead");
	fwupd_device_set_created(dev, 1);
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fwupd_device_set_id(dev, "USB:foo");
	fwupd_device_set_modified(dev, 60 * 60 * 24);
	fwupd_device_set_name(dev, "ColorHug2");
	fwupd_device_add_guid(dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_device_add_guid(dev, "00000000-0000-0000-0000-000000000000");
	fwupd_device_add_instance_id(dev, "USB\\VID_1234&PID_0001");
	fwupd_device_add_icon(dev, "input-gaming");
	fwupd_device_add_icon(dev, "input-mouse");
	fwupd_device_add_vendor_id(dev, "USB:0x1234");
	fwupd_device_add_vendor_id(dev, "PCI:0x5678");
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE | FWUPD_DEVICE_FLAG_REQUIRE_AC);
	g_assert_true(fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_true(fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_HISTORICAL));
	rel = fwupd_release_new();
	fwupd_release_add_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD);
	fwupd_release_add_checksum(rel, "deadbeef");
	fwupd_release_set_description(rel, "<p>Hi there!</p>");
	fwupd_release_set_filename(rel, "firmware.bin");
	fwupd_release_set_appstream_id(rel, "org.dave.ColorHug.firmware");
	fwupd_release_set_size(rel, 1024);
	fwupd_release_add_location(rel, "http://foo.com");
	fwupd_release_add_location(rel, "ftp://foo.com");
	fwupd_release_add_tag(rel, "vendor-2021q1");
	fwupd_release_add_tag(rel, "vendor-2021q2");
	fwupd_release_set_version(rel, "1.2.3");
	fwupd_device_add_release(dev, rel);
	str = fwupd_device_to_string(dev);
	g_print("\n%s", str);

	/* check GUIDs */
	g_assert_true(fwupd_device_has_guid(dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad"));
	g_assert_true(fwupd_device_has_guid(dev, "00000000-0000-0000-0000-000000000000"));
	g_assert_false(fwupd_device_has_guid(dev, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));

	/* convert the new non-breaking space back into a normal space:
	 * https://gitlab.gnome.org/GNOME/glib/commit/76af5dabb4a25956a6c41a75c0c7feeee74496da */
	str_ascii = g_string_new(str);
	_g_string_replace(str_ascii, " ", " ");
	ret = fu_test_compare_lines(str_ascii->str,
				    "FwupdDevice:\n"
				    "  DeviceId:             USB:foo\n"
				    "  Name:                 ColorHug2\n"
				    "  Guid:                 18f514d2-c12e-581f-a696-cc6d6c271699 "
				    "← USB\\VID_1234&PID_0001 ⚠\n"
				    "  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
				    "  Guid:                 00000000-0000-0000-0000-000000000000\n"
				    "  Flags:                updatable|require-ac\n"
				    "  Checksum:             SHA1(beefdead)\n"
				    "  VendorId:             USB:0x1234\n"
				    "  VendorId:             PCI:0x5678\n"
				    "  Icon:                 input-gaming,input-mouse\n"
				    "  Created:              1970-01-01\n"
				    "  Modified:             1970-01-02\n"
				    "  \n"
				    "  [Release]\n"
				    "  AppstreamId:          org.dave.ColorHug.firmware\n"
				    "  Description:          <p>Hi there!</p>\n"
				    "  Version:              1.2.3\n"
				    "  Filename:             firmware.bin\n"
				    "  Checksum:             SHA1(deadbeef)\n"
				    "  Tags:                 vendor-2021q1\n"
				    "  Tags:                 vendor-2021q2\n"
				    "  Size:                 1.0 kB\n"
				    "  Uri:                  http://foo.com\n"
				    "  Uri:                  ftp://foo.com\n"
				    "  Flags:                trusted-payload\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* export to json */
	builder = json_builder_new();
	json_builder_begin_object(builder);
	fwupd_device_to_json_full(dev, builder, FWUPD_DEVICE_FLAG_TRUSTED);
	json_builder_end_object(builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	g_assert_nonnull(data);
	ret = fu_test_compare_lines(data,
				    "{\n"
				    "  \"Name\" : \"ColorHug2\",\n"
				    "  \"DeviceId\" : \"USB:foo\",\n"
				    "  \"InstanceIds\" : [\n"
				    "    \"USB\\\\VID_1234&PID_0001\"\n"
				    "  ],\n"
				    "  \"Guid\" : [\n"
				    "    \"2082b5e0-7a64-478a-b1b2-e3404fab6dad\",\n"
				    "    \"00000000-0000-0000-0000-000000000000\"\n"
				    "  ],\n"
				    "  \"Flags\" : [\n"
				    "    \"updatable\",\n"
				    "    \"require-ac\"\n"
				    "  ],\n"
				    "  \"Checksums\" : [\n"
				    "    \"beefdead\"\n"
				    "  ],\n"
				    "  \"VendorId\" : \"USB:0x1234|PCI:0x5678\",\n"
				    "  \"VendorIds\" : [\n"
				    "    \"USB:0x1234\",\n"
				    "    \"PCI:0x5678\"\n"
				    "  ],\n"
				    "  \"Icons\" : [\n"
				    "    \"input-gaming\",\n"
				    "    \"input-mouse\"\n"
				    "  ],\n"
				    "  \"Created\" : 1,\n"
				    "  \"Modified\" : 86400,\n"
				    "  \"Releases\" : [\n"
				    "    {\n"
				    "      \"AppstreamId\" : \"org.dave.ColorHug.firmware\",\n"
				    "      \"Description\" : \"<p>Hi there!</p>\",\n"
				    "      \"Version\" : \"1.2.3\",\n"
				    "      \"Filename\" : \"firmware.bin\",\n"
				    "      \"Checksum\" : [\n"
				    "        \"deadbeef\"\n"
				    "      ],\n"
				    "      \"Tags\" : [\n"
				    "        \"vendor-2021q1\",\n"
				    "        \"vendor-2021q2\"\n"
				    "      ],\n"
				    "      \"Size\" : 1024,\n"
				    "      \"Locations\" : [\n"
				    "        \"http://foo.com\",\n"
				    "        \"ftp://foo.com\"\n"
				    "      ],\n"
				    "      \"Uri\" : \"http://foo.com\",\n"
				    "      \"Flags\" : [\n"
				    "        \"trusted-payload\"\n"
				    "      ]\n"
				    "    }\n"
				    "  ]\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* incorporate */
	fwupd_device_incorporate(dev_new, dev);
	g_assert_true(fwupd_device_has_vendor_id(dev_new, "USB:0x1234"));
	g_assert_true(fwupd_device_has_vendor_id(dev_new, "PCI:0x5678"));
	g_assert_true(fwupd_device_has_instance_id(dev_new, "USB\\VID_1234&PID_0001"));
}

static void
fwupd_client_devices_func(void)
{
	FwupdDevice *dev;
	gboolean ret;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error = NULL;

	client = fwupd_client_new();

	/* only run if running fwupd is new enough */
	ret = fwupd_client_connect(client, NULL, &error);
	if (ret == FALSE && (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMED_OUT) ||
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))) {
		g_debug("%s", error->message);
		g_test_skip("timeout connecting to daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);
	if (fwupd_client_get_daemon_version(client) == NULL) {
		g_test_skip("no enabled fwupd daemon");
		return;
	}
	if (!g_str_has_prefix(fwupd_client_get_daemon_version(client), "1.")) {
		g_test_skip("running fwupd is too old");
		return;
	}

	array = fwupd_client_get_devices(client, NULL, &error);
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
		g_test_skip("no available fwupd devices");
		return;
	}
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no available fwupd daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(array);
	g_assert_cmpint(array->len, >, 0);

	/* check device */
	dev = g_ptr_array_index(array, 0);
	g_assert_true(FWUPD_IS_DEVICE(dev));
	g_assert_cmpstr(fwupd_device_get_guid_default(dev), !=, NULL);
	g_assert_cmpstr(fwupd_device_get_id(dev), !=, NULL);
}

static void
fwupd_client_remotes_func(void)
{
	gboolean ret;
	g_autofree gchar *remotesdir = NULL;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(FwupdRemote) remote3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;

	remotesdir = g_test_build_filename(G_TEST_DIST, "tests", "remotes.d", NULL);
	(void)g_setenv("FU_SELF_TEST_REMOTES_DIR", remotesdir, TRUE);

	client = fwupd_client_new();

	/* only run if running fwupd is new enough */
	ret = fwupd_client_connect(client, NULL, &error);
	if (ret == FALSE && (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMED_OUT) ||
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))) {
		g_debug("%s", error->message);
		g_test_skip("timeout connecting to daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);
	if (fwupd_client_get_daemon_version(client) == NULL) {
		g_test_skip("no enabled fwupd daemon");
		return;
	}
	if (!g_str_has_prefix(fwupd_client_get_daemon_version(client), "1.")) {
		g_test_skip("running fwupd is too old");
		return;
	}

	array = fwupd_client_get_remotes(client, NULL, &error);
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
		g_test_skip("no available fwupd remotes");
		return;
	}
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no available fwupd daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(array);
	g_assert_cmpint(array->len, >, 0);

	/* check we can find the right thing */
	remote2 = fwupd_client_get_remote_by_id(client, "lvfs", NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote2);
	g_assert_cmpstr(fwupd_remote_get_id(remote2), ==, "lvfs");
	g_assert_nonnull(fwupd_remote_get_metadata_uri(remote2));

	/* check we set an error when unfound */
	remote3 = fwupd_client_get_remote_by_id(client, "XXXX", NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(remote3);
}

static gboolean
fwupd_has_system_bus(void)
{
	g_autoptr(GDBusConnection) conn = NULL;
	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (conn != NULL)
		return TRUE;
	g_debug("D-Bus system bus unavailable, skipping tests.");
	return FALSE;
}

static void
fwupd_common_machine_hash_func(void)
{
	gsize sz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *mhash1 = NULL;
	g_autofree gchar *mhash2 = NULL;
	g_autoptr(GError) error = NULL;

	if (!g_file_test("/etc/machine-id", G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing /etc/machine-id");
		return;
	}
	if (!g_file_get_contents("/etc/machine-id", &buf, &sz, &error)) {
		g_test_skip("/etc/machine-id is unreadable");
		return;
	}

	if (sz == 0) {
		g_test_skip("Empty /etc/machine-id");
		return;
	}

	mhash1 = fwupd_build_machine_id("salt1", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(mhash1, !=, NULL);
	mhash2 = fwupd_build_machine_id("salt2", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(mhash2, !=, NULL);
	g_assert_cmpstr(mhash2, !=, mhash1);
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
	g_assert_true(ret);
	g_assert_no_error(error);
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
	g_assert_true(ret);
	g_assert_no_error(error);
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

static gchar *
fwupd_security_attr_to_json_string(FwupdSecurityAttr *attr, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;
	json_builder_begin_object(builder);
	fwupd_security_attr_to_json(attr, builder);
	json_builder_end_object(builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to convert security attribute to json.");
		return NULL;
	}
	return g_steal_pointer(&data);
}

static void
fwupd_security_attr_func(void)
{
	gboolean ret;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *str3 = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdSecurityAttr) attr1 = fwupd_security_attr_new("org.fwupd.hsi.bar");
	g_autoptr(FwupdSecurityAttr) attr2 = fwupd_security_attr_new(NULL);
	g_autoptr(FwupdSecurityAttr) attr3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	for (guint i = 1; i < FWUPD_SECURITY_ATTR_RESULT_LAST; i++) {
		const gchar *tmp = fwupd_security_attr_result_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_security_attr_result_from_string(tmp), ==, i);
	}

	g_assert_cmpstr(fwupd_security_attr_get_appstream_id(attr1), ==, "org.fwupd.hsi.bar");
	fwupd_security_attr_set_appstream_id(attr1, "org.fwupd.hsi.baz");
	g_assert_cmpstr(fwupd_security_attr_get_appstream_id(attr1), ==, "org.fwupd.hsi.baz");

	fwupd_security_attr_set_level(attr1, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	g_assert_cmpint(fwupd_security_attr_get_level(attr1),
			==,
			FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);

	fwupd_security_attr_set_result(attr1, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_cmpint(fwupd_security_attr_get_result(attr1),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);

	fwupd_security_attr_add_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	fwupd_security_attr_remove_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	g_assert_true(fwupd_security_attr_has_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_false(fwupd_security_attr_has_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA));
	g_assert_false(fwupd_security_attr_has_flag(attr1, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));

	fwupd_security_attr_set_name(attr1, "DCI");
	g_assert_cmpstr(fwupd_security_attr_get_name(attr1), ==, "DCI");

	fwupd_security_attr_set_plugin(attr1, "uefi-capsule");
	g_assert_cmpstr(fwupd_security_attr_get_plugin(attr1), ==, "uefi-capsule");

	fwupd_security_attr_set_url(attr1, "https://foo.bar");
	g_assert_cmpstr(fwupd_security_attr_get_url(attr1), ==, "https://foo.bar");

	fwupd_security_attr_add_guid(attr1, "af3fc12c-d090-5783-8a67-845b90d3cfec");
	g_assert_true(fwupd_security_attr_has_guid(attr1, "af3fc12c-d090-5783-8a67-845b90d3cfec"));
	g_assert_false(fwupd_security_attr_has_guid(attr1, "NOT_GOING_TO_EXIST"));

	fwupd_security_attr_add_metadata(attr1, "KEY", "VALUE");
	g_assert_cmpstr(fwupd_security_attr_get_metadata(attr1, "KEY"), ==, "VALUE");

	/* remove this from the output */
	fwupd_security_attr_set_created(attr1, 0);

	str1 = fwupd_security_attr_to_string(attr1);
	ret = fu_test_compare_lines(str1,
				    "  AppstreamId:          org.fwupd.hsi.baz\n"
				    "  HsiLevel:             2\n"
				    "  HsiResult:            enabled\n"
				    "  Flags:                success\n"
				    "  Name:                 DCI\n"
				    "  Plugin:               uefi-capsule\n"
				    "  Uri:                  https://foo.bar\n"
				    "  Guid:                 af3fc12c-d090-5783-8a67-845b90d3cfec\n"
				    "  KEY:                  VALUE\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* roundtrip GVariant */
	data = fwupd_security_attr_to_variant(attr1);
	attr3 = fwupd_security_attr_from_variant(data);
	fwupd_security_attr_set_created(attr3, 0);
	str3 = fwupd_security_attr_to_string(attr3);
	ret = fu_test_compare_lines(str3,
				    "  AppstreamId:          org.fwupd.hsi.baz\n"
				    "  HsiLevel:             2\n"
				    "  HsiResult:            enabled\n"
				    "  Flags:                success\n"
				    "  Name:                 DCI\n"
				    "  Plugin:               uefi-capsule\n"
				    "  Uri:                  https://foo.bar\n"
				    "  Guid:                 af3fc12c-d090-5783-8a67-845b90d3cfec\n"
				    "  KEY:                  VALUE\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to JSON */
	json = fwupd_security_attr_to_json_string(attr1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(json,
				    "{\n"
				    "  \"AppstreamId\" : \"org.fwupd.hsi.baz\",\n"
				    "  \"HsiLevel\" : 2,\n"
				    "  \"HsiResult\" : \"enabled\",\n"
				    "  \"Name\" : \"DCI\",\n"
				    "  \"Plugin\" : \"uefi-capsule\",\n"
				    "  \"Uri\" : \"https://foo.bar\",\n"
				    "  \"Flags\" : [\n"
				    "    \"success\"\n"
				    "  ],\n"
				    "  \"Guid\" : [\n"
				    "    \"af3fc12c-d090-5783-8a67-845b90d3cfec\"\n"
				    "  ],\n"
				    "  \"KEY\" : \"VALUE\"\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* from JSON */
	ret = json_parser_load_from_data(parser, json, -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_security_attr_from_json(attr2, json_parser_get_root(parser), &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	/* we don't load unconditionally load metadata from the JSON */
	fwupd_security_attr_add_metadata(attr2, "KEY", "VALUE");

	str2 = fwupd_security_attr_to_string(attr2);
	ret = fu_test_compare_lines(str2, str1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/fwupd/enums", fwupd_enums_func);
	g_test_add_func("/fwupd/common{machine-hash}", fwupd_common_machine_hash_func);
	g_test_add_func("/fwupd/common{device-id}", fwupd_common_device_id_func);
	g_test_add_func("/fwupd/common{guid}", fwupd_common_guid_func);
	g_test_add_func("/fwupd/release", fwupd_release_func);
	g_test_add_func("/fwupd/plugin", fwupd_plugin_func);
	g_test_add_func("/fwupd/request", fwupd_request_func);
	g_test_add_func("/fwupd/device", fwupd_device_func);
	g_test_add_func("/fwupd/security-attr", fwupd_security_attr_func);
	g_test_add_func("/fwupd/remote{download}", fwupd_remote_download_func);
	g_test_add_func("/fwupd/remote{base-uri}", fwupd_remote_baseuri_func);
	g_test_add_func("/fwupd/remote{no-path}", fwupd_remote_nopath_func);
	g_test_add_func("/fwupd/remote{local}", fwupd_remote_local_func);
	g_test_add_func("/fwupd/remote{duplicate}", fwupd_remote_duplicate_func);
	g_test_add_func("/fwupd/remote{auth}", fwupd_remote_auth_func);
	if (fwupd_has_system_bus()) {
		g_test_add_func("/fwupd/client{remotes}", fwupd_client_remotes_func);
		g_test_add_func("/fwupd/client{devices}", fwupd_client_devices_func);
	}
	return g_test_run();
}
