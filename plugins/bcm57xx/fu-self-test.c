/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-dict-image.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-stage1-image.h"
#include "fu-bcm57xx-stage2-image.h"

static void
fu_bcm57xx_create_verbuf(guint8 *bufver, gsize bufsz, const gchar *version)
{
	gboolean ret;
	gsize versionsz = strlen(version) + 1;
	g_autoptr(GError) error = NULL;

	ret = fu_memcpy_safe(bufver,
			     bufsz,
			     0x0,
			     (const guint8 *)version,
			     versionsz,
			     0x0,
			     versionsz,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_bcm57xx_common_veritem_func(void)
{
	g_autoptr(FuBcm57xxVeritem) veritem1 = NULL;
	g_autoptr(FuBcm57xxVeritem) veritem2 = NULL;
	g_autoptr(FuBcm57xxVeritem) veritem3 = NULL;
	guint8 bufver[16] = {0x0};

	fu_bcm57xx_create_verbuf(bufver, sizeof(bufver), "5719-v1.43");
	veritem1 = fu_bcm57xx_veritem_new(bufver, sizeof(bufver));
	g_assert_nonnull(veritem1);
	g_assert_cmpstr(veritem1->version, ==, "1.43");
	g_assert_cmpstr(veritem1->branch, ==, BCM_FW_BRANCH_UNKNOWN);
	g_assert_cmpint(veritem1->verfmt, ==, FWUPD_VERSION_FORMAT_PAIR);

	fu_bcm57xx_create_verbuf(bufver, sizeof(bufver), "stage1-0.4.391");
	veritem2 = fu_bcm57xx_veritem_new(bufver, sizeof(bufver));
	g_assert_nonnull(veritem2);
	g_assert_cmpstr(veritem2->version, ==, "0.4.391");
	g_assert_cmpstr(veritem2->branch, ==, BCM_FW_BRANCH_OSS_FIRMWARE);
	g_assert_cmpint(veritem2->verfmt, ==, FWUPD_VERSION_FORMAT_TRIPLET);

	fu_bcm57xx_create_verbuf(bufver, sizeof(bufver), "RANDOM-7");
	veritem3 = fu_bcm57xx_veritem_new(bufver, sizeof(bufver));
	g_assert_nonnull(veritem3);
	g_assert_cmpstr(veritem3->version, ==, "RANDOM-7");
	g_assert_cmpstr(veritem3->branch, ==, BCM_FW_BRANCH_UNKNOWN);
	g_assert_cmpint(veritem3->verfmt, ==, FWUPD_VERSION_FORMAT_UNKNOWN);
}

static void
fu_bcm57xx_firmware_talos_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *fn_out = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_out = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;
	g_autoptr(FuFirmware) firmware = fu_bcm57xx_firmware_new();

	/* load file */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "Bcm5719_talos.bin", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("missing file");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_firmware_parse_bytes(firmware,
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH |
					  FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	images = fu_firmware_get_images(firmware);
	g_assert_cmpint(images->len, ==, 6);

	blob_out = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_out);
	fn_out = g_test_build_filename(G_TEST_BUILT, "tests", "Bcm5719_talos.bin", NULL);
	ret = fu_bytes_set_contents(fn_out, blob_out, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_bytes_compare(blob, blob_out, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_bcm57xx_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "bcm57xx.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "a3ac108905c37857cf48612b707c1c72c582f914",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* tests go here */
	g_type_ensure(FU_TYPE_BCM57XX_FIRMWARE);
	g_test_add_func("/fwupd/bcm57xx/firmware{xml}", fu_bcm57xx_firmware_xml_func);
	g_test_add_func("/fwupd/bcm57xx/firmware{talos}", fu_bcm57xx_firmware_talos_func);
	g_test_add_func("/fwupd/bcm57xx/common{veritem}", fu_bcm57xx_common_veritem_func);
	return g_test_run();
}
