/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bytes.h"
#include "fu-tpm-eventlog-common.h"
#include "fu-tpm-eventlog-item.h"
#include "fu-tpm-eventlog.h"

G_DEFINE_TYPE(FuTpmEventlog, fu_tpm_eventlog, FU_TYPE_FIRMWARE)

/**
 * fu_tpm_eventlog_calc_checksums:
 * @self: a #FuTpmEventlog
 * @pcr: a PCR value
 * @error: (nullable): optional return location for an error
 *
 * Calculate the possible checksums for a given PCR.
 *
 * Returns: (element-type utf8) (transfer container): checksum strings
 *
 * Since: 2.1.1
 **/
GPtrArray *
fu_tpm_eventlog_calc_checksums(FuTpmEventlog *self, guint8 pcr, GError **error)
{
	guint cnt_sha1 = 0;
	guint cnt_sha256 = 0;
	guint cnt_sha384 = 0;
	guint8 digest_sha1[FU_TPM_DIGEST_SIZE_SHA1] = {0x0};
	guint8 digest_sha256[FU_TPM_DIGEST_SIZE_SHA256] = {0x0};
	guint8 digest_sha384[FU_TPM_DIGEST_SIZE_SHA384] = {0x0};
	gsize digest_sha1_len = sizeof(digest_sha1);
	gsize digest_sha256_len = sizeof(digest_sha256);
	gsize digest_sha384_len = sizeof(digest_sha384);
	g_autoptr(GPtrArray) csums = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) items = fu_firmware_get_images(FU_FIRMWARE(self));

	/* sanity check */
	if (items->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no event log data");
		return NULL;
	}

	/* take existing PCR hash, append new measurement to that,
	 * hash that with the same algorithm */
	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(items, i);
		FuTpmEventlogItemKind item_kind = fu_tpm_eventlog_item_get_kind(item);
		guint8 item_pcr = fu_tpm_eventlog_item_get_pcr(item);
		g_autoptr(GBytes) item_blob = fu_firmware_get_bytes(FU_FIRMWARE(item), NULL);
		g_autoptr(GBytes) item_checksum_sha1 = NULL;
		g_autoptr(GBytes) item_checksum_sha256 = NULL;
		g_autoptr(GBytes) item_checksum_sha384 = NULL;

		if (item_pcr != pcr)
			continue;

		/* if TXT is enabled then the first event for PCR0 should be a StartupLocality */
		if (item_kind == FU_TPM_EVENTLOG_ITEM_KIND_NO_ACTION && item_pcr == 0 &&
		    item_blob != NULL && i == 0) {
			g_autoptr(FuStructTpmEfiStartupLocalityEvent) st_loc = NULL;
			st_loc = fu_struct_tpm_efi_startup_locality_event_parse_bytes(item_blob,
										      0x0,
										      NULL);
			if (st_loc != NULL) {
				guint8 locality =
				    fu_struct_tpm_efi_startup_locality_event_get_locality(st_loc);
				digest_sha384[FU_TPM_DIGEST_SIZE_SHA384 - 1] = locality;
				digest_sha256[FU_TPM_DIGEST_SIZE_SHA256 - 1] = locality;
				digest_sha1[FU_TPM_DIGEST_SIZE_SHA1 - 1] = locality;
				continue;
			}
		}

		item_checksum_sha1 = fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA1, NULL);
		if (item_checksum_sha1 != NULL) {
			g_autoptr(GChecksum) csum_sha1 = g_checksum_new(G_CHECKSUM_SHA1);
			g_checksum_update(csum_sha1, (const guchar *)digest_sha1, digest_sha1_len);
			g_checksum_update(
			    csum_sha1,
			    (const guchar *)g_bytes_get_data(item_checksum_sha1, NULL),
			    g_bytes_get_size(item_checksum_sha1));
			g_checksum_get_digest(csum_sha1, digest_sha1, &digest_sha1_len);
			cnt_sha1++;
		}
		item_checksum_sha256 =
		    fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA256, NULL);
		if (item_checksum_sha256 != NULL) {
			g_autoptr(GChecksum) csum_sha256 = g_checksum_new(G_CHECKSUM_SHA256);
			g_checksum_update(csum_sha256,
					  (const guchar *)digest_sha256,
					  digest_sha256_len);
			g_checksum_update(
			    csum_sha256,
			    (const guchar *)g_bytes_get_data(item_checksum_sha256, NULL),
			    g_bytes_get_size(item_checksum_sha256));
			g_checksum_get_digest(csum_sha256, digest_sha256, &digest_sha256_len);
			cnt_sha256++;
		}
		item_checksum_sha384 =
		    fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA384, NULL);
		if (item_checksum_sha384 != NULL) {
			g_autoptr(GChecksum) csum_sha384 = g_checksum_new(G_CHECKSUM_SHA384);
			g_checksum_update(csum_sha384,
					  (const guchar *)digest_sha384,
					  digest_sha384_len);
			g_checksum_update(
			    csum_sha384,
			    (const guchar *)g_bytes_get_data(item_checksum_sha384, NULL),
			    g_bytes_get_size(item_checksum_sha384));
			g_checksum_get_digest(csum_sha384, digest_sha384, &digest_sha384_len);
			cnt_sha384++;
		}
	}
	if (cnt_sha1 == 0 && cnt_sha256 == 0 && cnt_sha384 == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no SHA1, SHA256, or SHA384 data");
		return NULL;
	}
	if (cnt_sha1 > 0) {
		g_autoptr(GBytes) blob_sha1 = NULL;
		blob_sha1 = g_bytes_new_static(digest_sha1, sizeof(digest_sha1));
		g_ptr_array_add(csums, fu_bytes_to_string(blob_sha1));
	}
	if (cnt_sha256 > 0) {
		g_autoptr(GBytes) blob_sha256 = NULL;
		blob_sha256 = g_bytes_new_static(digest_sha256, sizeof(digest_sha256));
		g_ptr_array_add(csums, fu_bytes_to_string(blob_sha256));
	}
	if (cnt_sha384 > 0) {
		g_autoptr(GBytes) blob_sha384 = NULL;
		blob_sha384 = g_bytes_new_static(digest_sha384, sizeof(digest_sha384));
		g_ptr_array_add(csums, fu_bytes_to_string(blob_sha384));
	}
	return g_steal_pointer(&csums);
}

static void
fu_tpm_eventlog_init(FuTpmEventlog *self)
{
}

static void
fu_tpm_eventlog_class_init(FuTpmEventlogClass *klass)
{
}
