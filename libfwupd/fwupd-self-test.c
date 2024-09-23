/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <locale.h>
#include <string.h>

#include "fwupd-bios-setting.h"
#include "fwupd-client-private.h"
#include "fwupd-client-sync.h"
#include "fwupd-codec.h"
#include "fwupd-common.h"
#include "fwupd-device-private.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-plugin.h"
#include "fwupd-release.h"
#include "fwupd-remote-private.h"
#include "fwupd-report.h"
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
	if (g_pattern_match_simple(txt2, txt1))
		return TRUE;

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
	for (guint64 i = 1; i <= FWUPD_DEVICE_PROBLEM_IN_USE; i *= 2) {
		const gchar *tmp = fwupd_device_problem_to_string(i);
		if (tmp == NULL)
			g_warning("missing device problem 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_device_problem_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY; i *= 2) {
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
	for (guint64 i = 1; i <= FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION; i *= 2) {
		const gchar *tmp = fwupd_feature_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing feature flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_feature_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_RELEASE_FLAG_TRUSTED_REPORT; i *= 2) {
		const gchar *tmp = fwupd_release_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing release flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_release_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE; i *= 2) {
		const gchar *tmp = fwupd_request_flag_to_string(i);
		if (tmp == NULL)
			g_warning("missing request flag 0x%x", (guint)i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_request_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i < FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE; i *= 2) {
		const gchar *tmp = fwupd_remote_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_cmpint(fwupd_remote_flag_from_string(tmp), ==, i);
	}
}

static void
fwupd_release_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdRelease) release1 = NULL;
	g_autoptr(FwupdRelease) release2 = fwupd_release_new();
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
	data = fwupd_codec_to_variant(FWUPD_CODEC(release1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(release2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
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
	json1 = fwupd_codec_to_json_string(FWUPD_CODEC(release1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	json2 = fwupd_codec_to_json_string(FWUPD_CODEC(release2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = fu_test_compare_lines(json1, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to string */
	str = fwupd_codec_to_string(FWUPD_CODEC(release2));
	ret = fu_test_compare_lines(str,
				    "FwupdRelease:\n"
				    "  AppstreamId:          appstream-id\n"
				    "  ReleaseId:            id\n"
				    "  RemoteId:             remote-id\n"
				    "  Name:                 name\n"
				    "  NameVariantSuffix:    name_variant_suffix\n"
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
fwupd_report_func(void)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdReport) report1 = NULL;
	g_autoptr(FwupdReport) report2 = fwupd_report_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	report1 = fwupd_report_new();
	fwupd_report_add_metadata_item(report1, "foo", "bar");
	fwupd_report_add_metadata_item(report1, "baz", "bam");
	fwupd_report_set_version_old(report1, "1.2.3");
	fwupd_report_set_created(report1, 5678);
	fwupd_report_set_vendor(report1, "acme");
	fwupd_report_set_vendor_id(report1, 2468);
	fwupd_report_set_device_name(report1, "name");
	fwupd_report_set_distro_id(report1, "distro_id");
	fwupd_report_set_distro_version(report1, "distro_version");
	fwupd_report_set_remote_id(report1, "lvfs");
	data = fwupd_codec_to_variant(FWUPD_CODEC(report1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(report2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_report_get_metadata_item(report2, "foo"), ==, "bar");
	g_assert_cmpstr(fwupd_report_get_metadata_item(report2, "baz"), ==, "bam");
	g_assert_cmpstr(fwupd_report_get_version_old(report2), ==, "1.2.3");
	g_assert_cmpstr(fwupd_report_get_vendor(report2), ==, "acme");
	g_assert_cmpint(fwupd_report_get_vendor_id(report2), ==, 2468);
	g_assert_cmpstr(fwupd_report_get_device_name(report2), ==, "name");
	g_assert_cmpstr(fwupd_report_get_distro_id(report2), ==, "distro_id");
	g_assert_cmpstr(fwupd_report_get_distro_version(report2), ==, "distro_version");
	g_assert_cmpint(fwupd_report_get_created(report2), ==, 5678);

	/* to JSON */
	json1 = fwupd_codec_to_json_string(FWUPD_CODEC(report1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	json2 = fwupd_codec_to_json_string(FWUPD_CODEC(report2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = fu_test_compare_lines(json1, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to string */
	str = fwupd_codec_to_string(FWUPD_CODEC(report2));
	ret = fu_test_compare_lines(str,
				    "FwupdReport:\n"
				    "  DeviceName:           name\n"
				    "  DistroId:             distro_id\n"
				    "  DistroVersion:        distro_version\n"
				    "  VersionOld:           1.2.3\n"
				    "  Vendor:               acme\n"
				    "  VendorId:             2468\n"
				    "  RemoteId:             lvfs\n"
				    "  Flags:                none\n"
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
	g_autoptr(FwupdPlugin) plugin1 = fwupd_plugin_new();
	g_autoptr(FwupdPlugin) plugin2 = fwupd_plugin_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	fwupd_plugin_set_name(plugin1, "foo");
	fwupd_plugin_set_flags(plugin1, FWUPD_PLUGIN_FLAG_USER_WARNING);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	fwupd_plugin_add_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	fwupd_plugin_remove_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	fwupd_plugin_remove_flag(plugin1, FWUPD_PLUGIN_FLAG_NO_HARDWARE);
	data = fwupd_codec_to_variant(FWUPD_CODEC(plugin1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(plugin2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_plugin_get_name(plugin2), ==, "foo");
	g_assert_cmpint(fwupd_plugin_get_flags(plugin2),
			==,
			FWUPD_PLUGIN_FLAG_USER_WARNING | FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
	g_assert_true(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_USER_WARNING));
	g_assert_true(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE));
	g_assert_false(fwupd_plugin_has_flag(plugin2, FWUPD_PLUGIN_FLAG_NO_HARDWARE));

	str = fwupd_codec_to_string(FWUPD_CODEC(plugin2));
	ret = fu_test_compare_lines(str,
				    "FwupdPlugin:\n"
				    "  Name:                 foo\n"
				    "  Flags:                user-warning|clear-updatable\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_request_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	g_autoptr(FwupdRequest) request2 = fwupd_request_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	/* create dummy */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_DO_NOT_POWER_OFF);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	g_assert_cmpstr(fwupd_request_get_message(request),
			==,
			"Do not turn off your computer or remove the AC adaptor.");
	fwupd_request_set_message(request, "foo");
	fwupd_request_set_image(request, "bar");
	fwupd_request_set_device_id(request, "950da62d4c753a26e64f7f7d687104ce38e32ca5");
	str = fwupd_codec_to_string(FWUPD_CODEC(request));
	g_debug("%s", str);

	g_assert_true(fwupd_request_has_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE));
	g_assert_cmpint(fwupd_request_get_flags(request),
			==,
			FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_remove_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	g_assert_false(fwupd_request_has_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE));
	g_assert_cmpint(fwupd_request_get_flags(request), ==, FWUPD_REQUEST_FLAG_NONE);

	/* set in init */
	g_assert_cmpint(fwupd_request_get_created(request), >, 0);

	/* to serialized and back again */
	data = fwupd_codec_to_variant(FWUPD_CODEC(request), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(request2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_request_get_kind(request2), ==, FWUPD_REQUEST_KIND_IMMEDIATE);
	g_assert_cmpint(fwupd_request_get_created(request2), >, 0);
	g_assert_cmpstr(fwupd_request_get_id(request2), ==, FWUPD_REQUEST_ID_DO_NOT_POWER_OFF);
	g_assert_cmpstr(fwupd_request_get_message(request2), ==, "foo");
	g_assert_cmpstr(fwupd_request_get_image(request2), ==, "bar");
	g_assert_cmpstr(fwupd_request_get_device_id(request2),
			==,
			"950da62d4c753a26e64f7f7d687104ce38e32ca5");
}

static void
fwupd_device_filter_func(void)
{
	g_autoptr(FwupdDevice) dev = fwupd_device_new();
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED);

	/* none */
	g_assert_true(
	    fwupd_device_match_flags(dev, FWUPD_DEVICE_FLAG_NONE, FWUPD_DEVICE_FLAG_NONE));

	/* include */
	g_assert_true(fwupd_device_match_flags(dev,
					       FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD,
					       FWUPD_DEVICE_FLAG_NONE));
	g_assert_true(
	    fwupd_device_match_flags(dev, FWUPD_DEVICE_FLAG_SUPPORTED, FWUPD_DEVICE_FLAG_NONE));
	g_assert_true(
	    fwupd_device_match_flags(dev,
				     FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD | FWUPD_DEVICE_FLAG_SUPPORTED,
				     FWUPD_DEVICE_FLAG_NONE));
	g_assert_false(fwupd_device_match_flags(dev,
						FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED,
						FWUPD_DEVICE_FLAG_NONE));

	/* exclude, i.e. ~flag */
	g_assert_false(fwupd_device_match_flags(dev,
						FWUPD_DEVICE_FLAG_NONE,
						FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD));
	g_assert_false(
	    fwupd_device_match_flags(dev, FWUPD_DEVICE_FLAG_NONE, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_false(fwupd_device_match_flags(dev,
						FWUPD_DEVICE_FLAG_NONE,
						FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD |
						    FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fwupd_device_match_flags(dev,
					       FWUPD_DEVICE_FLAG_NONE,
					       FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED));
}

static void
fwupd_common_history_report_func(void)
{
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
	fwupd_device_set_plugin(dev, "colorhug");
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
	g_assert_cmpstr(json,
			==,
			"{\n"
			"  \"ReportType\" : \"history\",\n"
			"  \"ReportVersion\" : 2,\n"
			"  \"Metadata\" : {\n"
			"    \"DistroId\" : \"generic\",\n"
			"    \"DistroVariant\" : \"workstation\",\n"
			"    \"DistroVersion\" : \"39\"\n"
			"  },\n"
			"  \"Reports\" : [\n"
			"    {\n"
			"      \"Checksum\" : \"beefdead\",\n"
			"      \"ChecksumDevice\" : [\n"
			"        \"beefdead\"\n"
			"      ],\n"
			"      \"ReleaseId\" : \"123\",\n"
			"      \"UpdateState\" : 3,\n"
			"      \"UpdateError\" : \"device dead\",\n"
			"      \"UpdateMessage\" : \"oops\",\n"
			"      \"Guid\" : [\n"
			"        \"2082b5e0-7a64-478a-b1b2-e3404fab6dad\"\n"
			"      ],\n"
			"      \"Plugin\" : \"colorhug\",\n"
			"      \"VersionOld\" : \"1.2.3\",\n"
			"      \"VersionNew\" : \"1.2.4\",\n"
			"      \"Flags\" : 0,\n"
			"      \"Created\" : 0,\n"
			"      \"Modified\" : 0\n"
			"    }\n"
			"  ]\n"
			"}");
}

static void
fwupd_device_func(void)
{
	gboolean ret;
	g_autofree gchar *data = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdDevice) dev2 = fwupd_device_new();
	g_autoptr(FwupdDevice) dev_new = fwupd_device_new();
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str_ascii = NULL;

	/* create dummy object */
	dev = fwupd_device_new();
	fwupd_device_add_checksum(dev, "beefdead");
	fwupd_device_set_created(dev, 1);
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fwupd_device_set_id(dev, "0000000000000000000000000000000000000000");
	fwupd_device_set_modified(dev, 60 * 60 * 24);
	fwupd_device_set_name(dev, "ColorHug2");
	fwupd_device_set_branch(dev, "community");
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
	str = fwupd_codec_to_string(FWUPD_CODEC(dev));
	g_print("\n%s", str);

	/* check GUIDs */
	g_assert_true(fwupd_device_has_guid(dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad"));
	g_assert_true(fwupd_device_has_guid(dev, "00000000-0000-0000-0000-000000000000"));
	g_assert_false(fwupd_device_has_guid(dev, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));

	/* convert the new non-breaking space back into a normal space:
	 * https://gitlab.gnome.org/GNOME/glib/commit/76af5dabb4a25956a6c41a75c0c7feeee74496da */
	str_ascii = g_string_new(str);
	g_string_replace(str_ascii, " ", " ", 0);
	ret = fu_test_compare_lines(
	    str_ascii->str,
	    "FwupdDevice:\n"
	    "  DeviceId:             0000000000000000000000000000000000000000\n"
	    "  Name:                 ColorHug2\n"
	    "  Guid:                 18f514d2-c12e-581f-a696-cc6d6c271699 "
	    "← USB\\VID_1234&PID_0001 ⚠\n"
	    "  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
	    "  Guid:                 00000000-0000-0000-0000-000000000000\n"
	    "  Branch:               community\n"
	    "  Flags:                updatable|require-ac\n"
	    "  Checksum:             SHA1(beefdead)\n"
	    "  VendorId:             USB:0x1234\n"
	    "  VendorId:             PCI:0x5678\n"
	    "  Icon:                 input-gaming,input-mouse\n"
	    "  Created:              1970-01-01\n"
	    "  Modified:             1970-01-02\n"
	    "  FwupdRelease:\n"
	    "    AppstreamId:        org.dave.ColorHug.firmware\n"
	    "    Description:        <p>Hi there!</p>\n"
	    "    Version:            1.2.3\n"
	    "    Filename:           firmware.bin\n"
	    "    Checksum:           SHA1(deadbeef)\n"
	    "    Tags:               vendor-2021q1\n"
	    "    Tags:               vendor-2021q2\n"
	    "    Size:               1.0 kB\n"
	    "    Uri:                http://foo.com\n"
	    "    Uri:                ftp://foo.com\n"
	    "    Flags:              trusted-payload\n",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* export to json */
	data = fwupd_codec_to_json_string(FWUPD_CODEC(dev), FWUPD_CODEC_FLAG_TRUSTED, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	ret =
	    fu_test_compare_lines(data,
				  "{\n"
				  "  \"Name\" : \"ColorHug2\",\n"
				  "  \"DeviceId\" : \"0000000000000000000000000000000000000000\",\n"
				  "  \"InstanceIds\" : [\n"
				  "    \"USB\\\\VID_1234&PID_0001\"\n"
				  "  ],\n"
				  "  \"Guid\" : [\n"
				  "    \"2082b5e0-7a64-478a-b1b2-e3404fab6dad\",\n"
				  "    \"00000000-0000-0000-0000-000000000000\"\n"
				  "  ],\n"
				  "  \"Branch\" : \"community\",\n"
				  "  \"Flags\" : [\n"
				  "    \"updatable\",\n"
				  "    \"require-ac\"\n"
				  "  ],\n"
				  "  \"Checksums\" : [\n"
				  "    \"beefdead\"\n"
				  "  ],\n"
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

	/* from JSON */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(dev2), data, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fwupd_device_has_vendor_id(dev2, "USB:0x1234"));
	g_assert_true(fwupd_device_has_instance_id(dev2, "USB\\VID_1234&PID_0001"));
	g_assert_true(fwupd_device_has_flag(dev2, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fwupd_device_has_flag(dev2, FWUPD_DEVICE_FLAG_LOCKED));
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
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
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
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
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
	g_autoptr(FwupdSecurityAttr) attr3 = fwupd_security_attr_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

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

	str1 = fwupd_codec_to_string(FWUPD_CODEC(attr1));
	ret = fu_test_compare_lines(str1,
				    "FwupdSecurityAttr:\n"
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
	data = fwupd_codec_to_variant(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(attr3), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fwupd_security_attr_set_created(attr3, 0);
	str3 = fwupd_codec_to_string(FWUPD_CODEC(attr3));
	ret = fu_test_compare_lines(str3,
				    "FwupdSecurityAttr:\n"
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
	json = fwupd_codec_to_json_string(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE, &error);
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
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(attr2), json, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	/* we don't load unconditionally load metadata from the JSON */
	fwupd_security_attr_add_metadata(attr2, "KEY", "VALUE");

	str2 = fwupd_codec_to_string(FWUPD_CODEC(attr2));
	ret = fu_test_compare_lines(str2, str1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_bios_settings_func(void)
{
	gboolean ret;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *str3 = NULL;
	g_autofree gchar *str4 = NULL;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autoptr(FwupdBiosSetting) attr1 = fwupd_bios_setting_new("foo", "/path/to/bar");
	g_autoptr(FwupdBiosSetting) attr2 = fwupd_bios_setting_new(NULL, NULL);
	g_autoptr(FwupdBiosSetting) attr3 = fwupd_bios_setting_new(NULL, NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data1 = NULL;
	g_autoptr(GVariant) data2 = NULL;

	g_assert_cmpstr(fwupd_bios_setting_get_name(attr1), ==, "foo");
	fwupd_bios_setting_set_name(attr1, "UEFISecureBoot");
	g_assert_cmpstr(fwupd_bios_setting_get_name(attr1), ==, "UEFISecureBoot");

	fwupd_bios_setting_set_kind(attr1, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	g_assert_cmpint(fwupd_bios_setting_get_kind(attr1),
			==,
			FWUPD_BIOS_SETTING_KIND_ENUMERATION);

	fwupd_bios_setting_set_description(attr1, "Controls Secure boot");
	g_assert_cmpstr(fwupd_bios_setting_get_description(attr1), ==, "Controls Secure boot");
	fwupd_bios_setting_set_current_value(attr1, "Disabled");
	g_assert_cmpstr(fwupd_bios_setting_get_current_value(attr1), ==, "Disabled");

	fwupd_bios_setting_add_possible_value(attr1, "Disabled");
	fwupd_bios_setting_add_possible_value(attr1, "Enabled");
	g_assert_true(fwupd_bios_setting_has_possible_value(attr1, "Disabled"));
	g_assert_false(fwupd_bios_setting_has_possible_value(attr1, "NOT_GOING_TO_EXIST"));

	str1 = fwupd_codec_to_string(FWUPD_CODEC(attr1));
	ret = fu_test_compare_lines(str1,
				    "FwupdBiosSetting:\n"
				    "  Name:                 UEFISecureBoot\n"
				    "  Description:          Controls Secure boot\n"
				    "  Filename:             /path/to/bar\n"
				    "  BiosSettingType:      1\n"
				    "  BiosSettingCurrentValue: Disabled\n"
				    "  BiosSettingReadOnly:  False\n"
				    "  BiosSettingPossibleValues: Disabled\n"
				    "  BiosSettingPossibleValues: Enabled\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* roundtrip GVariant */
	data1 = fwupd_codec_to_variant(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_TRUSTED);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(attr2), data1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str2 = fwupd_codec_to_string(FWUPD_CODEC(attr2));
	ret = fu_test_compare_lines(str2,
				    "FwupdBiosSetting:\n"
				    "  Name:                 UEFISecureBoot\n"
				    "  Description:          Controls Secure boot\n"
				    "  Filename:             /path/to/bar\n"
				    "  BiosSettingType:      1\n"
				    "  BiosSettingCurrentValue: Disabled\n"
				    "  BiosSettingReadOnly:  False\n"
				    "  BiosSettingPossibleValues: Disabled\n"
				    "  BiosSettingPossibleValues: Enabled\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to JSON */
	json1 = fwupd_codec_to_json_string(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	ret = fu_test_compare_lines(json1,
				    "{\n"
				    "  \"Name\" : \"UEFISecureBoot\",\n"
				    "  \"Description\" : \"Controls Secure boot\",\n"
				    "  \"Filename\" : \"/path/to/bar\",\n"
				    "  \"BiosSettingCurrentValue\" : \"Disabled\",\n"
				    "  \"BiosSettingReadOnly\" : false,\n"
				    "  \"BiosSettingType\" : 1,\n"
				    "  \"BiosSettingPossibleValues\" : [\n"
				    "    \"Disabled\",\n"
				    "    \"Enabled\"\n"
				    "  ]\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* from JSON */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(attr2), json1, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	str3 = fwupd_codec_to_string(FWUPD_CODEC(attr2));
	ret = fu_test_compare_lines(str3, str1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure we filter CurrentValue if not trusted */
	data2 = fwupd_codec_to_variant(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(attr3), data2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str4 = fwupd_codec_to_string(FWUPD_CODEC(attr3));
	ret = fu_test_compare_lines(str4,
				    "FwupdBiosSetting:\n"
				    "  Name:                 UEFISecureBoot\n"
				    "  Description:          Controls Secure boot\n"
				    "  Filename:             /path/to/bar\n"
				    "  BiosSettingType:      1\n"
				    "  BiosSettingReadOnly:  False\n"
				    "  BiosSettingPossibleValues: Disabled\n"
				    "  BiosSettingPossibleValues: Enabled\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* convert to JSON */
	json2 = fwupd_codec_to_json_string(FWUPD_CODEC(attr1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = fu_test_compare_lines(json2,
				    "{\n"
				    "  \"Name\" : \"UEFISecureBoot\",\n"
				    "  \"Description\" : \"Controls Secure boot\",\n"
				    "  \"Filename\" : \"/path/to/bar\",\n"
				    "  \"BiosSettingCurrentValue\" : \"Disabled\",\n"
				    "  \"BiosSettingReadOnly\" : false,\n"
				    "  \"BiosSettingType\" : 1,\n"
				    "  \"BiosSettingPossibleValues\" : [\n"
				    "    \"Disabled\",\n"
				    "    \"Enabled\"\n"
				    "  ]\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	setlocale(LC_ALL, "");
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);

	g_assert_cmpint(sizeof(FwupdDeviceFlags), ==, sizeof(guint64));
	g_assert_cmpint(sizeof(FwupdStatus), ==, sizeof(guint32));

	/* tests go here */
	g_test_add_func("/fwupd/enums", fwupd_enums_func);
	g_test_add_func("/fwupd/common{device-id}", fwupd_common_device_id_func);
	g_test_add_func("/fwupd/common{guid}", fwupd_common_guid_func);
	g_test_add_func("/fwupd/common{history-report}", fwupd_common_history_report_func);
	g_test_add_func("/fwupd/release", fwupd_release_func);
	g_test_add_func("/fwupd/report", fwupd_report_func);
	g_test_add_func("/fwupd/plugin", fwupd_plugin_func);
	g_test_add_func("/fwupd/request", fwupd_request_func);
	g_test_add_func("/fwupd/device", fwupd_device_func);
	g_test_add_func("/fwupd/device{filter}", fwupd_device_filter_func);
	g_test_add_func("/fwupd/security-attr", fwupd_security_attr_func);
	g_test_add_func("/fwupd/bios-attrs", fwupd_bios_settings_func);
	if (fwupd_has_system_bus()) {
		g_test_add_func("/fwupd/client{remotes}", fwupd_client_remotes_func);
		g_test_add_func("/fwupd/client{devices}", fwupd_client_devices_func);
	}
	return g_test_run();
}
