/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-mtd-device.h"
#include "fu-udev-device-private.h"

typedef struct {
	FuContext *ctx;
	FuDevice *device;
} FuTest;

static void
fu_test_free(FuTest *self)
{
	if (self->ctx != NULL)
		g_object_unref(self->ctx);
	if (self->device != NULL)
		g_object_unref(self->device);
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_free)

static FuDevice *
fu_test_mtd_find_mtdram(FuContext *ctx, GError **error)
{
	g_autoptr(GPtrArray) mtd_files = NULL;
	g_autoptr(FuDevice) device = NULL;
	const gchar *device_file;

	mtd_files = fu_path_glob("/sys/devices/virtual/mtd", "mtd?", error);
	if (mtd_files == NULL) {
		g_prefix_error_literal(error, "no mtdram device: ");
		return NULL;
	}

	/* create device */
	device_file = g_ptr_array_index(mtd_files, 0);
	device = g_object_new(FU_TYPE_MTD_DEVICE, "context", ctx, "backend-id", device_file, NULL);
	if (!fu_device_probe(device, error))
		return NULL;
	if (g_strcmp0(fu_device_get_name(device), "mtdram test device") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "device is not mtdram test device");
		return NULL;
	}
	return g_steal_pointer(&device);
}

static void
fu_test_mtd_device_raw_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gsize bufsz;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GRand) rand = g_rand_new_with_seed(0);

	bufsz = fu_device_get_firmware_size_max(self->device);
	g_assert_cmpint(bufsz, >=, 0x400000);

	/* create a random payload exactly the same size */
	for (gsize i = 0; i < bufsz; i++)
		fu_byte_array_append_uint8(buf, g_rand_int_range(rand, 0x00, 0xFF));
	fw = g_bytes_new(buf->data, buf->len);

	/* write with a verify */
	firmware = fu_firmware_new_from_bytes(fw);
	ret = fu_device_write_firmware(self->device,
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* dump back */
	fu_progress_reset(progress);
	fw2 = fu_device_dump_firmware(self->device, progress, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw2);

	/* verify */
	ret = fu_bytes_compare(fw, fw2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static FuFirmware *
fu_test_mtd_setup_mtdram(FuTest *self, GType firmware_gtype, const gchar *filename_xml)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = g_object_new(firmware_gtype, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* write the IFD image */
	filename = g_test_build_filename(G_TEST_DIST, "tests", filename_xml, NULL);
	g_debug("loading from %s", filename);
	ret = fu_firmware_build_from_filename(firmware, filename, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	fu_firmware_set_bytes(firmware, blob);
	ret = fu_device_write_firmware(self->device,
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* success */
	fu_device_probe_invalidate(self->device);
	ret = fu_device_set_quirk_kv(self->device,
				     "MtdMetadataSize",
				     "0",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_device_set_firmware_gtype(self->device, firmware_gtype);
	return g_steal_pointer(&firmware);
}

static void
fu_test_mtd_device_ifd_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) device_bios = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware_bios = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;

	/* write the IFD image */
	firmware = fu_test_mtd_setup_mtdram(self, FU_TYPE_IFD_FIRMWARE, "mtd-ifd.builder.xml");
	g_assert_nonnull(firmware);

	/* re-probe image */
	ret = fu_device_setup(self->device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	device_bios = fu_device_get_child_by_logical_id(self->device, "bios", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_bios);

	/* just for debugging */
	str = fu_device_to_string(self->device);
	g_debug("%s", str);

	/* re-write just the BIOS */
	firmware_bios = fu_firmware_get_image_by_id(firmware, "bios", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_bios);
	ret = fu_device_write_firmware(device_bios,
				       firmware_bios,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_fmap_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;

	/* write the FMAP image */
	firmware = fu_test_mtd_setup_mtdram(self, FU_TYPE_FMAP_FIRMWARE, "mtd-fmap.builder.xml");
	g_assert_nonnull(firmware);

	/* re-probe image */
	ret = fu_device_set_quirk_kv(self->device,
				     "MtdFmapOffset",
				     "0x0",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_setup(self->device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_version(self->device), ==, "456");

	/* re-write firmware, this time in chunks */
	ret = fu_device_set_quirk_kv(self->device,
				     "MtdFmapRegions",
				     "SBOM,FMAP",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_write_firmware(self->device,
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_uswid_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	/* write the FMAP image */
	firmware = fu_test_mtd_setup_mtdram(self, FU_TYPE_USWID_FIRMWARE, "mtd-uswid.builder.xml");
	g_assert_nonnull(firmware);

	/* re-probe image */
	ret = fu_device_setup(self->device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_version(self->device), ==, "456");
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTest) self = g_new0(FuTest, 1);
	g_autoptr(FuDeviceLocker) locker = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_MTD_VERBOSE", "1", TRUE);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("CONFIGURATION_DIRECTORY", testdatadir, TRUE);

	/* do not save silo */
	self->ctx = fu_context_new();
	ret = fu_context_load_quirks(self->ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(self->ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* find correct device */
	self->device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (self->device == NULL) {
		g_test_skip(error->message);
		return 0;
	}
	locker = fu_device_locker_new(self->device, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
	    g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no permission to read mtdram device");
		return 0;
	}
	g_assert_no_error(error);
	g_assert_nonnull(locker);

	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_data_func("/mtd/device{raw}", self, fu_test_mtd_device_raw_func);
	g_test_add_data_func("/mtd/device{uswid}", self, fu_test_mtd_device_uswid_func);
	g_test_add_data_func("/mtd/device{ifd}", self, fu_test_mtd_device_ifd_func);
	g_test_add_data_func("/mtd/device{fmap}", self, fu_test_mtd_device_fmap_func);
	return g_test_run();
}
