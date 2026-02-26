/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_tpm_eventlog_func(void)
{
	gboolean ret;
	const gchar *csum_sha1;
	g_autoptr(FuTpmEventlogItem) item1 = fu_tpm_eventlog_item_new();
	g_autoptr(FuTpmEventlogItem) item2 = fu_tpm_eventlog_item_new();
	g_autoptr(FuTpmEventlog) log = fu_tpm_eventlog_v1_new();
	g_autoptr(GBytes) blob = g_bytes_new_static("hello", 5);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) checksums1 = NULL;
	g_autoptr(GPtrArray) checksums2 = NULL;

	/* one event */
	fu_tpm_eventlog_item_set_kind(item1, FU_TPM_EVENTLOG_ITEM_KIND_EFI_ACTION);
	fu_tpm_eventlog_item_set_pcr(item1, 0);
	fu_tpm_eventlog_item_add_checksum(item1, FU_TPM_ALG_SHA1, blob);
	ret = fu_firmware_add_image(FU_FIRMWARE(log), FU_FIRMWARE(item1), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	checksums1 = fu_tpm_eventlog_calc_checksums(log, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksums1);
	g_assert_cmpint(checksums1->len, ==, 1);
	csum_sha1 = fwupd_checksum_get_by_kind(checksums1, G_CHECKSUM_SHA1);
	g_assert_cmpstr(csum_sha1, ==, "2942632a0231d481bf40564515998dd72c01c118");

	/* one event + no-action = same checksum */
	fu_tpm_eventlog_item_set_kind(item2, FU_TPM_EVENTLOG_ITEM_KIND_NO_ACTION);
	fu_tpm_eventlog_item_set_pcr(item2, 0);
	fu_tpm_eventlog_item_add_checksum(item2, FU_TPM_ALG_SHA1, blob);
	ret = fu_firmware_add_image(FU_FIRMWARE(log), FU_FIRMWARE(item2), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	checksums2 = fu_tpm_eventlog_calc_checksums(log, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksums2);
	g_assert_cmpint(checksums2->len, ==, 1);
	csum_sha1 = fwupd_checksum_get_by_kind(checksums2, G_CHECKSUM_SHA1);
	g_assert_cmpstr(csum_sha1, ==, "2942632a0231d481bf40564515998dd72c01c118");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/tpm-eventlog", fu_tpm_eventlog_func);
	return g_test_run();
}
