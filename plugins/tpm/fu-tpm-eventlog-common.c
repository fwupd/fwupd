/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-tpm-eventlog-common.h"

const gchar *
fu_tpm_eventlog_pcr_to_string(gint pcr)
{
	if (pcr == 0)
		return "BIOS";
	if (pcr == 1)
		return "BIOS Configuration";
	if (pcr == 2)
		return "Option ROMs";
	if (pcr == 3)
		return "Option ROM configuration";
	if (pcr == 4)
		return "Initial program loader code";
	if (pcr == 5)
		return "Initial program loader code configuration";
	if (pcr == 6)
		return "State transitions and wake events";
	if (pcr == 7)
		return "Platform manufacturer specific measurements";
	if (pcr >= 8 && pcr <= 15)
		return "Static operating system";
	if (pcr == 16)
		return "Debug";
	if (pcr == 17)
		return "Dynamic root of trust measurement and launch control policy";
	if (pcr >= 18 && pcr <= 22)
		return "Trusted OS";
	if (pcr == 23)
		return "Application support";
	return "Undefined";
}

guint32
fu_tpm_eventlog_hash_get_size(TPM2_ALG_ID hash_kind)
{
	if (hash_kind == TPM2_ALG_SHA1)
		return TPM2_SHA1_DIGEST_SIZE;
	if (hash_kind == TPM2_ALG_SHA256)
		return TPM2_SHA256_DIGEST_SIZE;
	if (hash_kind == TPM2_ALG_SHA384)
		return TPM2_SHA384_DIGEST_SIZE;
	if (hash_kind == TPM2_ALG_SHA512)
		return TPM2_SHA512_DIGEST_SIZE;
	return 0;
}

gchar *
fu_tpm_eventlog_strhex(GBytes *blob)
{
	GString *csum = g_string_new(NULL);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);
	for (guint i = 0; i < bufsz; i++)
		g_string_append_printf(csum, "%02x", buf[i]);
	return g_string_free(csum, FALSE);
}

gchar *
fu_tpm_eventlog_blobstr(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, NULL);
	return g_base64_encode((const guchar *)g_bytes_get_data(blob, NULL),
			       g_bytes_get_size(blob));
}

GPtrArray *
fu_tpm_eventlog_calc_checksums(GPtrArray *items, guint8 pcr, GError **error)
{
	guint cnt_sha1 = 0;
	guint cnt_sha256 = 0;
	guint cnt_sha384 = 0;
	guint8 digest_sha1[TPM2_SHA1_DIGEST_SIZE] = {0x0};
	guint8 digest_sha256[TPM2_SHA256_DIGEST_SIZE] = {0x0};
	guint8 digest_sha384[TPM2_SHA384_DIGEST_SIZE] = {0x0};
	gsize digest_sha1_len = sizeof(digest_sha1);
	gsize digest_sha256_len = sizeof(digest_sha256);
	gsize digest_sha384_len = sizeof(digest_sha384);
	g_autoptr(GPtrArray) csums = g_ptr_array_new_with_free_func(g_free);

	/* sanity check */
	if (items->len == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no event log data");
		return NULL;
	}

	/* take existing PCR hash, append new measurement to that,
	 * hash that with the same algorithm */
	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(items, i);
		if (item->pcr != pcr)
			continue;

		/* if TXT is enabled then the first event for PCR0 should be a StartupLocality */
		if (item->kind == FU_TPM_EVENTLOG_ITEM_KIND_EV_NO_ACTION && item->pcr == 0 &&
		    item->blob != NULL && i == 0) {
			g_autoptr(GByteArray) st_loc = NULL;
			st_loc = fu_struct_tpm_efi_startup_locality_event_parse(
			    g_bytes_get_data(item->blob, NULL),
			    g_bytes_get_size(item->blob),
			    0x0,
			    NULL);
			if (st_loc != NULL) {
				guint8 locality =
				    fu_struct_tpm_efi_startup_locality_event_get_locality(st_loc);
				digest_sha384[TPM2_SHA384_DIGEST_SIZE - 1] = locality;
				digest_sha256[TPM2_SHA256_DIGEST_SIZE - 1] = locality;
				digest_sha1[TPM2_SHA1_DIGEST_SIZE - 1] = locality;
				continue;
			}
		}

		if (item->checksum_sha1 != NULL) {
			g_autoptr(GChecksum) csum_sha1 = g_checksum_new(G_CHECKSUM_SHA1);
			g_checksum_update(csum_sha1, (const guchar *)digest_sha1, digest_sha1_len);
			g_checksum_update(
			    csum_sha1,
			    (const guchar *)g_bytes_get_data(item->checksum_sha1, NULL),
			    g_bytes_get_size(item->checksum_sha1));
			g_checksum_get_digest(csum_sha1, digest_sha1, &digest_sha1_len);
			cnt_sha1++;
		}
		if (item->checksum_sha256 != NULL) {
			g_autoptr(GChecksum) csum_sha256 = g_checksum_new(G_CHECKSUM_SHA256);
			g_checksum_update(csum_sha256,
					  (const guchar *)digest_sha256,
					  digest_sha256_len);
			g_checksum_update(
			    csum_sha256,
			    (const guchar *)g_bytes_get_data(item->checksum_sha256, NULL),
			    g_bytes_get_size(item->checksum_sha256));
			g_checksum_get_digest(csum_sha256, digest_sha256, &digest_sha256_len);
			cnt_sha256++;
		}
		if (item->checksum_sha384 != NULL) {
			g_autoptr(GChecksum) csum_sha384 = g_checksum_new(G_CHECKSUM_SHA384);
			g_checksum_update(csum_sha384,
					  (const guchar *)digest_sha384,
					  digest_sha384_len);
			g_checksum_update(
			    csum_sha384,
			    (const guchar *)g_bytes_get_data(item->checksum_sha384, NULL),
			    g_bytes_get_size(item->checksum_sha384));
			g_checksum_get_digest(csum_sha384, digest_sha384, &digest_sha384_len);
			cnt_sha384++;
		}
	}
	if (cnt_sha1 == 0 && cnt_sha256 == 0 && cnt_sha384 == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no SHA1, SHA256, or SHA384 data");
		return NULL;
	}
	if (cnt_sha1 > 0) {
		g_autoptr(GBytes) blob_sha1 = NULL;
		blob_sha1 = g_bytes_new_static(digest_sha1, sizeof(digest_sha1));
		g_ptr_array_add(csums, fu_tpm_eventlog_strhex(blob_sha1));
	}
	if (cnt_sha256 > 0) {
		g_autoptr(GBytes) blob_sha256 = NULL;
		blob_sha256 = g_bytes_new_static(digest_sha256, sizeof(digest_sha256));
		g_ptr_array_add(csums, fu_tpm_eventlog_strhex(blob_sha256));
	}
	if (cnt_sha384 > 0) {
		g_autoptr(GBytes) blob_sha384 = NULL;
		blob_sha384 = g_bytes_new_static(digest_sha384, sizeof(digest_sha384));
		g_ptr_array_add(csums, fu_tpm_eventlog_strhex(blob_sha384));
	}
	return g_steal_pointer(&csums);
}
