/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <string.h>

#include "fwupd-enums-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-bios-settings-private.h"
#include "fu-cab-firmware-private.h"
#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-device-progress.h"
#include "fu-dummy-efivars.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-efivars-private.h"
#include "fu-kernel-search-path-private.h"
#include "fu-plugin-private.h"
#include "fu-progress-private.h"
#include "fu-security-attrs-private.h"
#include "fu-self-test-device.h"
#include "fu-self-test-struct.h"
#include "fu-smbios-private.h"
#include "fu-test.h"
#include "fu-udev-device-private.h"
#include "fu-volume-private.h"

/* nocheck:magic-inlines=300 */

static void
fu_archive_invalid_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(archive);
}

static void
fu_archive_cab_func(void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) data_tmp1 = NULL;
	g_autoptr(GBytes) data_tmp2 = NULL;
	g_autoptr(GBytes) data_tmp3 = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "colorhug",
					 "colorhug-als-3.0.2.cab",
					 NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(archive);

	data_tmp1 = fu_archive_lookup_by_fn(archive, "firmware.metainfo.xml", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp1);
	checksum1 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp1);
	g_assert_cmpstr(checksum1, ==, "f62ee340c27bbb80229c3dd3cb2e78bddfc82d4f");

	data_tmp2 = fu_archive_lookup_by_fn(archive, "firmware.txt", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp2);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp2);
	g_assert_cmpstr(checksum2, ==, "22596363b3de40b06f981fb85d82312e8c0ed511");

	data_tmp3 = fu_archive_lookup_by_fn(archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(data_tmp3);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/archive/invalid", fu_archive_invalid_func);
	g_test_add_func("/fwupd/archive/cab", fu_archive_cab_func);
	return g_test_run();
}
