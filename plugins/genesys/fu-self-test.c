/*
 * Copyright 2026 Genesys Logic, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-usbhub-device.h"

/*
 * Test vectors use literal hardware offsets and version pairs.
 * nocheck:magic-inlines=80
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

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/genesys/p34/dual-bank-safety",
			fu_genesys_usbhub_device_p34_dual_bank_safety_func);
	g_test_add_func("/fwupd/genesys/p34/chunks-page-boundary",
			fu_genesys_usbhub_device_p34_chunks_page_boundary_func);
	return g_test_run();
}
