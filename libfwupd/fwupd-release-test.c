/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-release.h"
#include "fwupd-test.h"

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
	fwupd_release_set_sbom_url(release1, "sbom_url");
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
	g_assert_cmpstr(fwupd_release_get_sbom_url(release2), ==, "sbom_url");
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
				    "  Created:              1970-01-01 01:34:38\n"
				    "  Uri:                  location\n"
				    "  Homepage:             homepage\n"
				    "  DetailsUrl:           details_url\n"
				    "  SourceUrl:            source_url\n"
				    "  SbomUrl:              sbom_url\n"
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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/release", fwupd_release_func);
	return g_test_run();
}
