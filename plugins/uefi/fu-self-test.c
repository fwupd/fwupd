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
	g_assert_cmpint (fu_uefi_device_get_hardware_instance (dev), ==, 0x0);
	g_assert_cmpint (fu_uefi_device_get_version (dev), ==, 65586);
	g_assert_cmpint (fu_uefi_device_get_version_lowest (dev), ==, 65582);
	g_assert_cmpint (fu_uefi_device_get_version_error (dev), ==, 18472960);
	g_assert_cmpint (fu_uefi_device_get_capsule_flags (dev), ==, 0xfe);
	g_assert_cmpint (fu_uefi_device_get_status (dev), ==, FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL);

	/* check enums all converted */
	for (guint i = 0; i < FU_UEFI_DEVICE_STATUS_LAST; i++)
		g_assert_nonnull (fu_uefi_device_status_to_string (i));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/uefi/ucs2", fu_uefi_ucs2_func);
	g_test_add_func ("/uefi/device", fu_uefi_device_func);
	return g_test_run ();
}
