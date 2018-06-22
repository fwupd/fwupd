/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>

#include "fu-test.h"
#include "fu-ucs2.h"
#include "fu-uefi-bgrt.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device.h"

static void
fu_uefi_ucs2_func (void)
{
	g_autofree guint16 *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	str1 = fu_uft8_to_ucs2 ("hw!", -1);
	g_assert_cmpint (fu_ucs2_strlen (str1, -1), ==, 3);
	str2 = fu_ucs2_to_uft8 (str1, -1);
	g_assert_cmpstr ("hw!", ==, str2);
}

static void
fu_uefi_bgrt_func (void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuUefiBgrt) bgrt = fu_uefi_bgrt_new ();
	ret = fu_uefi_bgrt_setup (bgrt, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_true (fu_uefi_bgrt_get_supported (bgrt));
	g_assert_cmpint (fu_uefi_bgrt_get_xoffset (bgrt), ==, 123);
	g_assert_cmpint (fu_uefi_bgrt_get_yoffset (bgrt), ==, 456);
	g_assert_cmpint (fu_uefi_bgrt_get_width (bgrt), ==, 54);
	g_assert_cmpint (fu_uefi_bgrt_get_height (bgrt), ==, 24);
}

static void
fu_uefi_framebuffer_func (void)
{
	gboolean ret;
	guint32 height = 0;
	guint32 width = 0;
	g_autoptr(GError) error = NULL;
	ret = fu_uefi_get_framebuffer_size (&width, &height, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_cmpint (width, ==, 456);
	g_assert_cmpint (height, ==, 789);
}

static void
fu_uefi_bitmap_func (void)
{
	gboolean ret;
	gsize sz = 0;
	guint32 height = 0;
	guint32 width = 0;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *buf = NULL;
	g_autoptr(GError) error = NULL;

	fn = fu_test_get_filename (TESTDATADIR, "test.bmp");
	g_assert (fn != NULL);
	ret = g_file_get_contents (fn, &buf, &sz, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_nonnull (buf);
	ret = fu_uefi_get_bitmap_size ((guint8 *)buf, sz, &width, &height, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_cmpint (width, ==, 54);
	g_assert_cmpint (height, ==, 24);
}

static void
fu_uefi_device_func (void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuUefiDevice) dev = NULL;

	fn = fu_test_get_filename (TESTDATADIR, "efi/esrt/entries/entry0");
	g_assert (fn != NULL);
	dev = fu_uefi_device_new_from_entry (fn);
	g_assert_nonnull (dev);

	g_assert_cmpint (fu_uefi_device_get_kind (dev), ==, FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE);
	g_assert_cmpstr (fu_uefi_device_get_guid (dev), ==, "ddc0ee61-e7f0-4e7d-acc5-c070a398838e");
	g_assert_cmpint (fu_uefi_device_get_version (dev), ==, 65586);
	g_assert_cmpint (fu_uefi_device_get_version_lowest (dev), ==, 65582);
	g_assert_cmpint (fu_uefi_device_get_version_error (dev), ==, 18472960);
	g_assert_cmpint (fu_uefi_device_get_capsule_flags (dev), ==, 0xfe);
	g_assert_cmpint (fu_uefi_device_get_status (dev), ==, FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL);

	/* check enums all converted */
	for (guint i = 0; i < FU_UEFI_DEVICE_STATUS_LAST; i++)
		g_assert_nonnull (fu_uefi_device_status_to_string (i));
}

static void
fu_uefi_plugin_func (void)
{
	FuUefiDevice *dev;
	g_autofree gchar *esrt_path = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) entries = NULL;

	/* add each device */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrt_path = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);
	entries = fu_uefi_get_esrt_entry_paths (esrt_path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (entries);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < entries->len; i++) {
		const gchar *path = g_ptr_array_index (entries, i);
		g_autoptr(FuUefiDevice) dev_tmp = fu_uefi_device_new_from_entry (path);
		g_ptr_array_add (devices, g_object_ref (dev_tmp));
	}
	g_assert_cmpint (devices->len, ==, 2);

	/* system firmware */
	dev = g_ptr_array_index (devices, 0);
	g_assert_cmpint (fu_uefi_device_get_kind (dev), ==, FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE);
	g_assert_cmpstr (fu_uefi_device_get_guid (dev), ==, "ddc0ee61-e7f0-4e7d-acc5-c070a398838e");
	g_assert_cmpint (fu_uefi_device_get_version (dev), ==, 65586);
	g_assert_cmpint (fu_uefi_device_get_version_lowest (dev), ==, 65582);
	g_assert_cmpint (fu_uefi_device_get_version_error (dev), ==, 18472960);
	g_assert_cmpint (fu_uefi_device_get_capsule_flags (dev), ==, 0xfe);
	g_assert_cmpint (fu_uefi_device_get_status (dev), ==, FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL);

	/* system firmware */
	dev = g_ptr_array_index (devices, 1);
	g_assert_cmpint (fu_uefi_device_get_kind (dev), ==, FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE);
	g_assert_cmpstr (fu_uefi_device_get_guid (dev), ==, "671d19d0-d43c-4852-98d9-1ce16f9967e4");
	g_assert_cmpint (fu_uefi_device_get_version (dev), ==, 3090287969);
	g_assert_cmpint (fu_uefi_device_get_version_lowest (dev), ==, 1);
	g_assert_cmpint (fu_uefi_device_get_version_error (dev), ==, 0);
	g_assert_cmpint (fu_uefi_device_get_capsule_flags (dev), ==, 32784);
	g_assert_cmpint (fu_uefi_device_get_status (dev), ==, FU_UEFI_DEVICE_STATUS_SUCCESS);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("FWUPD_SYSFSFWDIR", TESTDATADIR, TRUE);
	g_setenv ("FWUPD_SYSFSDRIVERDIR", TESTDATADIR, TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/uefi/ucs2", fu_uefi_ucs2_func);
	g_test_add_func ("/uefi/bgrt", fu_uefi_bgrt_func);
	g_test_add_func ("/uefi/framebuffer", fu_uefi_framebuffer_func);
	g_test_add_func ("/uefi/bitmap", fu_uefi_bitmap_func);
	g_test_add_func ("/uefi/device", fu_uefi_device_func);
	g_test_add_func ("/uefi/plugin", fu_uefi_plugin_func);
	return g_test_run ();
}
