/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-report.h"
#include "fwupd-test.h"

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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/report", fwupd_report_func);
	return g_test_run();
}
