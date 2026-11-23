/*
 * Copyright 2026 Genesys Logic, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cfi-device.h"
#include "fu-context-private.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"

/*
 * Test vectors use literal hardware offsets and version pairs.
 * nocheck:magic-inlines=120
 */

static void
fu_genesys_usbhub_device_p34_dual_bank_safety_func(void)
{
	gboolean backup = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) sig_err =
	    g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID, "bad sig");
	g_autoptr(GError) read_err =
	    g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_READ, "io failed");

	g_assert_cmpint(fu_genesys_usbhub_device_bank_state_from_error(sig_err),
			==,
			FU_GENESYS_USBHUB_BANK_STATE_INVALID);
	g_assert_cmpint(fu_genesys_usbhub_device_bank_state_from_error(read_err),
			==,
			FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN);
	{
		const guint8 bad_bank[] = {0xFF, 0xFF, 0xFF, 0xFF};
		const guint8 good_bank[] = {0x01, 0x02, 0x00, 0x03};
		g_assert_cmpint(
		    fu_genesys_usbhub_device_bank_state_from_image(bad_bank, sizeof(bad_bank)),
		    ==,
		    FU_GENESYS_USBHUB_BANK_STATE_INVALID);
		g_assert_cmpint(
		    fu_genesys_usbhub_device_bank_state_from_image(good_bank, sizeof(good_bank)),
		    ==,
		    FU_GENESYS_USBHUB_BANK_STATE_VALID);
		g_assert_cmpint(fu_genesys_usbhub_device_bank_state_from_image(good_bank, 1),
				==,
				FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN);
	}

	g_assert_false(fu_genesys_usbhub_device_select_update_bank2(TRUE, 0x8716, 0x8715, &backup));
	g_assert_true(backup);
	g_assert_true(fu_genesys_usbhub_device_select_update_bank2(FALSE, 0x8625, 0x8624, &backup));
	g_assert_false(backup);

	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x8625,
						       0x8624,
						       TRUE,
						       0x8626,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x8625,
						       0x8624,
						       TRUE,
						       0x8624,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_assert_true(fu_genesys_usbhub_device_check_update_bank(TRUE,
								 TRUE,
								 TRUE,
								 FU_GENESYS_USBHUB_BANK_STATE_VALID,
								 FU_GENESYS_USBHUB_BANK_STATE_VALID,
								 0x8625,
								 0x8624,
								 TRUE,
								 0x8626,
								 &error));
	g_assert_no_error(error);

	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x0000,
						       0x8624,
						       TRUE,
						       0x8625,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_assert_true(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x0000,
						       0x8624,
						       FALSE,
						       0x8625,
						       &error));
	g_assert_no_error(error);
}

static void
fu_genesys_usbhub_device_p34_chunks_page_boundary_func(void)
{
	g_autofree guint8 *buf = g_malloc0(0xB000);
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GError) error = NULL;
	FuChunk *chk0;
	FuChunk *chk1;

	chunks = fu_chunk_array_new(buf, 0xB000, 0xB000, 0x10000, 0xFFC0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunks);
	g_assert_cmpuint(chunks->len, ==, 2);

	chk0 = g_ptr_array_index(chunks, 0);
	g_assert_cmpuint(fu_chunk_get_page(chk0), ==, 0);
	g_assert_cmpuint(fu_chunk_get_address(chk0), ==, 0xB000);
	g_assert_cmpuint(fu_chunk_get_data_sz(chk0), ==, 0x5000);

	chk1 = g_ptr_array_index(chunks, 1);
	g_assert_cmpuint(fu_chunk_get_page(chk1), ==, 1);
	g_assert_cmpuint(fu_chunk_get_address(chk1), ==, 0x0000);
	g_assert_cmpuint(fu_chunk_get_data_sz(chk1), ==, 0x6000);
}

static void
fu_genesys_usbhub_device_p34_strict_project_check_func(void)
{
	const guint8 proj_a[15] = "P34WD40-HUB-L3 ";
	const guint8 proj_b[15] = "P34WD40-HUB-L2 ";
	const guint8 proj_blank[15] = {0};
	g_autoptr(GBytes) bytes_a = g_bytes_new_static(proj_a, sizeof(proj_a));
	g_autoptr(GBytes) bytes_b = g_bytes_new_static(proj_b, sizeof(proj_b));
	g_autoptr(GBytes) bytes_blank = g_bytes_new_static(proj_blank, sizeof(proj_blank));
	g_autoptr(GError) error = NULL;

	g_assert_true(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									   bytes_a,
									   "352350",
									   bytes_a,
									   "352350",
									   &error));
	g_assert_no_error(error);

	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_a,
									    "352350",
									    bytes_b,
									    "352350",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_clear_error(&error);

	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_a,
									    "352350",
									    bytes_a,
									    "352510",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_clear_error(&error);

	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_blank,
									    "352350",
									    bytes_blank,
									    "352350",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
}

static GBytes *
fu_genesys_test_build_fw_image(gsize static_off,
			       gsize project_off,
			       const gchar *ic_type,
			       const gchar *project_str)
{
	const gsize sz = 0x1000;
	g_autofree guint8 *buf = g_malloc0(sz);
	guint16 checksum = 0;

	buf[0xFB] = 0x04;
	g_assert_true(fu_memcpy_safe(buf, sz, 0xFC, (const guint8 *)"XROM", 4, 0, 4, NULL));
	buf[static_off + 0x00] = '8';
	g_assert_true(
	    fu_memcpy_safe(buf, sz, static_off + 0x08, (const guint8 *)ic_type, 6, 0, 6, NULL));
	g_assert_true(
	    fu_memcpy_safe(buf, sz, project_off, (const guint8 *)project_str, 15, 0, 15, NULL));
	buf[0x10E] = 0x25;
	buf[0x10F] = 0x86;
	for (gsize i = 0; i < sz - 2; i++)
		checksum += buf[i];
	buf[sz - 2] = (checksum >> 8) & 0xFF;
	buf[sz - 1] = checksum & 0xFF;
	return g_bytes_new_take(g_steal_pointer(&buf), sz);
}

static void
fu_genesys_usbhub_device_p34_firmware_project_parse_func(void)
{
	struct {
		gsize static_off;
		gsize project_off;
		const gchar *ic_type;
		const gchar *project;
	} specs[3] = {
	    {0x251, 0x2B1, "352510", "LNV_P34WD40_L10"},
	    {0x221, 0x281, "352350", "LNV_P34WD40_L20"},
	    {0x221, 0x281, "352350", "LNV_P34WD40_L30"},
	};
	g_autoptr(GPtrArray) parsed =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	for (guint i = 0; i < G_N_ELEMENTS(specs); i++) {
		g_autoptr(FuFirmware) fw = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
		g_autoptr(GBytes) blob = fu_genesys_test_build_fw_image(specs[i].static_off,
									specs[i].project_off,
									specs[i].ic_type,
									specs[i].project);
		g_autoptr(GBytes) proj = NULL;
		g_autofree gchar *ic = NULL;
		g_autoptr(GError) error = NULL;
		gsize projsz = 0;

		g_assert_true(fu_firmware_parse_bytes(fw,
						      blob,
						      0x0,
						      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
						      &error));
		g_assert_no_error(error);

		proj = fu_genesys_usbhub_firmware_get_project_bytes(FU_GENESYS_USBHUB_FIRMWARE(fw));
		ic = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
		    FU_GENESYS_USBHUB_FIRMWARE(fw));
		g_assert_nonnull(proj);
		g_assert_cmpuint(g_bytes_get_size(proj), ==, 15);
		g_assert_cmpmem(g_bytes_get_data(proj, &projsz), projsz, specs[i].project, 15);
		g_assert_cmpstr(ic, ==, specs[i].ic_type);
		g_ptr_array_add(parsed, g_steal_pointer(&fw));
	}

	for (guint dev_i = 0; dev_i < 3; dev_i++) {
		g_autoptr(GBytes) dev_proj = g_bytes_new_static(specs[dev_i].project, 15);
		for (guint fw_i = 0; fw_i < 3; fw_i++) {
			FuGenesysUsbhubFirmware *fw = g_ptr_array_index(parsed, fw_i);
			g_autoptr(GBytes) fw_proj =
			    fu_genesys_usbhub_firmware_get_project_bytes(fw);
			g_autofree gchar *fw_ic =
			    fu_genesys_usbhub_firmware_get_mask_project_ic_type(fw);
			g_autoptr(GError) error = NULL;
			gboolean ret = fu_genesys_usbhub_device_check_project_compatibility(
			    TRUE,
			    dev_proj,
			    specs[dev_i].ic_type,
			    fw_proj,
			    fw_ic,
			    &error);
			if (dev_i == fw_i) {
				g_assert_true(ret);
				g_assert_no_error(error);
			} else {
				g_assert_false(ret);
				g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
			}
		}
	}

	/* verify P34 flash chip C84012 (GD25Q20C) resolves from cfi.quirk; never silently skip */
	{
		g_autofree gchar *installed_cfi =
		    g_test_build_filename(G_TEST_DIST, "tests", "cfi", NULL);
		g_autofree gchar *source_cfi =
		    g_test_build_filename(G_TEST_DIST, "..", "..", "data", NULL);
		const gchar *cfi_dir =
		    g_file_test(installed_cfi, G_FILE_TEST_IS_DIR) ? installed_cfi : source_cfi;
		g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_CACHE);
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		g_autoptr(FuDevice) parent_dev = fu_device_new(ctx);
		g_autoptr(FuCfiDevice) cfi = NULL;
		g_autoptr(GError) error = NULL;

		g_assert_true(g_file_test(cfi_dir, G_FILE_TEST_IS_DIR));
		fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, cfi_dir);
		g_assert_true(fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error));
		g_assert_no_error(error);
		cfi = fu_cfi_device_new(parent_dev, "C84012");
		g_assert_true(fu_device_probe(FU_DEVICE(cfi), &error));
		g_assert_no_error(error);
		g_assert_cmpuint(fu_cfi_device_get_page_size(cfi), ==, 0x100);
		g_assert_cmpuint(fu_cfi_device_get_sector_size(cfi), ==, 0x1000);
		g_assert_cmpuint(fu_cfi_device_get_block_size(cfi), ==, 0x10000);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/genesys/p34/dual-bank-safety",
			fu_genesys_usbhub_device_p34_dual_bank_safety_func);
	g_test_add_func("/fwupd/genesys/p34/chunks-page-boundary",
			fu_genesys_usbhub_device_p34_chunks_page_boundary_func);
	g_test_add_func("/fwupd/genesys/p34/strict-project-check",
			fu_genesys_usbhub_device_p34_strict_project_check_func);
	g_test_add_func("/fwupd/genesys/p34/firmware-project-parse",
			fu_genesys_usbhub_device_p34_firmware_project_parse_func);
	return g_test_run();
}
