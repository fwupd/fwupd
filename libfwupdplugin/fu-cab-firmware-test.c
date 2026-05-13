/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cab-firmware-private.h"

static void
fu_cab_firmware_checksum_func(void)
{
	guint8 buf[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
	guint32 checksums[] = {
	    0xc0404040,
	    0x40604060,
	    0x40307070,
	    0x40302040,
	    0x40302010,
	    0x102030,
	    0x1020,
	    0x10,
	    0x0,
	};

	for (guint i = 0; i <= sizeof(buf); i++) {
		gboolean ret;
		guint32 checksum = 0x0;
		g_autoptr(GError) error = NULL;

		ret = fu_cab_firmware_compute_checksum(buf, sizeof(buf) - i, &checksum, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpint(checksum, ==, checksums[i]);
	}
}

static void
fu_cab_firmware_compressed_size_func(void)
{
	gboolean ret;
	g_autoptr(FuCabFirmware) cab2 = fu_cab_firmware_new();
	g_autoptr(FuCabFirmware) cab = fu_cab_firmware_new();
	g_autoptr(FuCabImage) img = fu_cab_image_new();
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) blob_img = g_bytes_new_static("abc", 3);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* build a cab file and then tweak the payload */
	fu_cab_firmware_set_compressed(cab, TRUE);
	fu_firmware_set_bytes(FU_FIRMWARE(img), blob_img);
	fu_firmware_set_id(FU_FIRMWARE(img), "foo.txt");
	ret = fu_firmware_add_image(FU_FIRMWARE(cab), FU_FIRMWARE(img), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write to a mutable buffer */
	blob = fu_firmware_write(FU_FIRMWARE(cab), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	buf = g_bytes_unref_to_array(g_steal_pointer(&blob));
	g_assert_nonnull(buf);
	g_assert_nonnull(buf->data);

	/* change FuStructCabData.uncomp to be too small */
	g_assert_cmpint(buf->len, ==, 0x53);
	g_assert_cmpint(buf->data[0x4A], ==, 0x03);
	buf->data[0x4A] = 0x02;

	/* parse the new blob */
	blob2 = g_bytes_new(buf->data, buf->len);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cab2),
				      blob2,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM,
				      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_true(g_str_has_prefix(error->message, "decompressed size mismatch"));
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/cab-firmware/checksum", fu_cab_firmware_checksum_func);
	g_test_add_func("/fwupd/cab-firmware/compressed-size",
			fu_cab_firmware_compressed_size_func);
	return g_test_run();
}
