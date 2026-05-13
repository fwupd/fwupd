/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_hid_descriptor_container_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) item_id = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "hid-descriptor2.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);

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
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuHidReport) report1 = NULL;
	g_autoptr(FuHidReport) report2 = NULL;
	g_autoptr(FuHidReport) report3 = NULL;
	g_autoptr(FuHidReport) report4 = NULL;
	g_autoptr(FuFirmware) item_usage = NULL;
	g_autoptr(FuFirmware) item_id = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "hid-descriptor.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);

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

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_HID_DESCRIPTOR);
	g_type_ensure(FU_TYPE_HID_REPORT_ITEM);
	g_test_add_func("/fwupd/hid-descriptor", fu_hid_descriptor_func);
	g_test_add_func("/fwupd/hid-descriptor/container", fu_hid_descriptor_container_func);
	return g_test_run();
}
