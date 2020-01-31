/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-tpm-eventlog-common.h"

const gchar *
fu_tpm_eventlog_pcr_to_string (gint pcr)
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

const gchar *
fu_tpm_eventlog_hash_to_string (TPM2_ALG_ID hash_kind)
{
	if (hash_kind == TPM2_ALG_SHA1)
		return "SHA1";
	if (hash_kind == TPM2_ALG_SHA256)
		return "SHA256";
	if (hash_kind == TPM2_ALG_SHA384)
		return "SHA384";
	if (hash_kind == TPM2_ALG_SHA512)
		return "SHA512";
	return NULL;
}

guint32
fu_tpm_eventlog_hash_get_size (TPM2_ALG_ID hash_kind)
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

const gchar *
fu_tpm_eventlog_item_kind_to_string (FuTpmEventlogItemKind event_type)
{
	if (event_type == EV_PREBOOT_CERT)
		return "EV_PREBOOT_CERT";
	if (event_type == EV_POST_CODE)
		return "EV_POST_CODE";
	if (event_type == EV_NO_ACTION)
		return "EV_NO_ACTION";
	if (event_type == EV_SEPARATOR)
		return "EV_SEPARATOR";
	if (event_type == EV_ACTION)
		return "EV_ACTION";
	if (event_type == EV_EVENT_TAG)
		return "EV_EVENT_TAG";
	if (event_type == EV_S_CRTM_CONTENTS)
		return "EV_S_CRTM_CONTENTS";
	if (event_type == EV_S_CRTM_VERSION)
		return "EV_S_CRTM_VERSION";
	if (event_type == EV_CPU_MICROCODE)
		return "EV_CPU_MICROCODE";
	if (event_type == EV_PLATFORM_CONFIG_FLAGS)
		return "EV_PLATFORM_CONFIG_FLAGS";
	if (event_type == EV_TABLE_OF_DEVICES)
		return "EV_TABLE_OF_DEVICES";
	if (event_type == EV_COMPACT_HASH)
		return "EV_COMPACT_HASH";
	if (event_type == EV_NONHOST_CODE)
		return "EV_NONHOST_CODE";
	if (event_type == EV_NONHOST_CONFIG)
		return "EV_NONHOST_CONFIG";
	if (event_type == EV_NONHOST_INFO)
		return "EV_NONHOST_INFO";
	if (event_type == EV_OMIT_BOOT_DEVICE_EVENTS)
		return "EV_OMIT_BOOT_DEVICE_EVENTS";
	if (event_type == EV_EFI_EVENT_BASE)
		return "EV_EFI_EVENT_BASE";
	if (event_type == EV_EFI_VARIABLE_DRIVER_CONFIG)
		return "EV_EFI_VARIABLE_DRIVER_CONFIG";
	if (event_type == EV_EFI_VARIABLE_BOOT)
		return "EV_EFI_VARIABLE_BOOT";
	if (event_type == EV_EFI_BOOT_SERVICES_APPLICATION)
		return "EV_BOOT_SERVICES_APPLICATION";
	if (event_type == EV_EFI_BOOT_SERVICES_DRIVER)
		return "EV_EFI_BOOT_SERVICES_DRIVER";
	if (event_type == EV_EFI_RUNTIME_SERVICES_DRIVER)
		return "EV_EFI_RUNTIME_SERVICES_DRIVER";
	if (event_type == EV_EFI_GPT_EVENT)
		return "EV_EFI_GPT_EVENT";
	if (event_type == EV_EFI_ACTION)
		return "EV_EFI_ACTION";
	if (event_type == EV_EFI_PLATFORM_FIRMWARE_BLOB)
		return "EV_EFI_PLATFORM_FIRMWARE_BLOB";
	if (event_type == EV_EFI_HANDOFF_TABLES)
		return "EV_EFI_HANDOFF_TABLES";
	if (event_type == EV_EFI_HCRTM_EVENT)
		return "EV_EFI_HCRTM_EVENT";
	if (event_type == EV_EFI_VARIABLE_AUTHORITY)
		return "EV_EFI_EFI_VARIABLE_AUTHORITY";
	return NULL;
}

gchar *
fu_tpm_eventlog_strhex (GBytes *blob)
{
	GString *csum = g_string_new (NULL);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (blob, &bufsz);
	for (guint i = 0; i < bufsz; i++)
		g_string_append_printf (csum, "%02x", buf[i]);
	return g_string_free (csum, FALSE);
}

gchar *
fu_tpm_eventlog_blobstr (GBytes *blob)
{
	gboolean has_printable = FALSE;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (blob, &bufsz);
	g_autoptr(GString) str = g_string_new (NULL);

	for (gsize i = 0; i < bufsz; i++) {
		gchar chr = buf[i];
		if (g_ascii_isprint (chr)) {
			g_string_append_c (str, chr);
			has_printable = TRUE;
		} else {
			g_string_append_c (str, '.');
		}
	}
	if (!has_printable)
		return NULL;
	return g_string_free (g_steal_pointer (&str), FALSE);
}

GPtrArray *
fu_tpm_eventlog_calc_checksums (GPtrArray *items, guint8 pcr, GError **error)
{
	guint cnt_sha1 = 0;
	guint cnt_sha256 = 0;
	guint8 digest_sha1[TPM2_SHA1_DIGEST_SIZE] = { 0x0 };
	guint8 digest_sha256[TPM2_SHA256_DIGEST_SIZE] = { 0x0 };
	gsize digest_sha1_len = sizeof(digest_sha1);
	gsize digest_sha256_len = sizeof(digest_sha256);
	g_autoptr(GPtrArray) csums = g_ptr_array_new_with_free_func (g_free);

	/* sanity check */
	if (items->len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "no event log data");
		return NULL;
	}

	/* take existing PCR hash, append new measurement to that,
	 * hash that with the same algorithm */
	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index (items, i);
		if (item->pcr != pcr)
			continue;
		if (item->checksum_sha1 != NULL) {
			g_autoptr(GChecksum) csum_sha1 = g_checksum_new (G_CHECKSUM_SHA1);
			g_checksum_update (csum_sha1,
					   (const guchar *) digest_sha1,
					   digest_sha1_len);
			g_checksum_update (csum_sha1,
					   (const guchar *) g_bytes_get_data (item->checksum_sha1, NULL),
					   g_bytes_get_size (item->checksum_sha1));
			g_checksum_get_digest (csum_sha1, digest_sha1, &digest_sha1_len);
			cnt_sha1++;
		}
		if (item->checksum_sha256 != NULL) {
			g_autoptr(GChecksum) csum_sha256 = g_checksum_new (G_CHECKSUM_SHA256);
			g_checksum_update (csum_sha256,
					   (const guchar *) digest_sha256,
					   digest_sha256_len);
			g_checksum_update (csum_sha256,
					   (const guchar *) g_bytes_get_data (item->checksum_sha256, NULL),
					   g_bytes_get_size (item->checksum_sha256));
			g_checksum_get_digest (csum_sha256, digest_sha256, &digest_sha256_len);
			cnt_sha256++;
		}
	}
	if (cnt_sha1 == 0 && cnt_sha256 == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "no SHA1 or SHA256 data");
		return NULL;
	}
	if (cnt_sha1 > 0) {
		g_autoptr(GBytes) blob_sha1 = NULL;
		blob_sha1 = g_bytes_new_static (digest_sha1, sizeof(digest_sha1));
		g_ptr_array_add (csums, fu_tpm_eventlog_strhex (blob_sha1));
	}
	if (cnt_sha256 > 0) {
		g_autoptr(GBytes) blob_sha256 = NULL;
		blob_sha256 = g_bytes_new_static (digest_sha256, sizeof(digest_sha256));
		g_ptr_array_add (csums, fu_tpm_eventlog_strhex (blob_sha256));
	}
	return g_steal_pointer (&csums);
}
