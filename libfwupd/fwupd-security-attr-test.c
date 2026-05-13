/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-error.h"
#include "fwupd-security-attr.h"
#include "fwupd-test.h"

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
	fwupd_security_attr_set_fwupd_version(attr1, "2.0.7");
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
	g_assert_cmpstr(fwupd_security_attr_get_fwupd_version(attr1), ==, "2.0.7");

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
				    "  Version:              2.0.7\n"
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
				    "  Version:              2.0.7\n"
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
				    "  \"AppstreamId\": \"org.fwupd.hsi.baz\",\n"
				    "  \"HsiLevel\": 2,\n"
				    "  \"HsiResult\": \"enabled\",\n"
				    "  \"Name\": \"DCI\",\n"
				    "  \"Plugin\": \"uefi-capsule\",\n"
				    "  \"Version\": \"2.0.7\",\n"
				    "  \"Uri\": \"https://foo.bar\",\n"
				    "  \"Flags\": [\n"
				    "    \"success\"\n"
				    "  ],\n"
				    "  \"Guid\": [\n"
				    "    \"af3fc12c-d090-5783-8a67-845b90d3cfec\"\n"
				    "  ],\n"
				    "  \"KEY\": \"VALUE\"\n"
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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/security-attr", fwupd_security_attr_func);
	return g_test_run();
}
