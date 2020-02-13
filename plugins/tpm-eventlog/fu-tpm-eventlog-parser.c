/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <tss2/tss2_esys.h>

#include "fu-common.h"

#include "fu-tpm-eventlog-parser.h"

#define FU_TPM_EVENTLOG_V1_IDX_PCR			0x00
#define FU_TPM_EVENTLOG_V1_IDX_TYPE			0x04
#define FU_TPM_EVENTLOG_V1_IDX_DIGEST			0x08
#define FU_TPM_EVENTLOG_V1_IDX_EVENT_SIZE		0x1c
#define FU_TPM_EVENTLOG_V1_SIZE				0x20

#define FU_TPM_EVENTLOG_V2_HDR_IDX_SIGNATURE		0x00
#define FU_TPM_EVENTLOG_V2_HDR_IDX_PLATFORM_CLASS	0x10
#define FU_TPM_EVENTLOG_V2_HDR_IDX_SPEC_VERSION_MINOR	0x14
#define FU_TPM_EVENTLOG_V2_HDR_IDX_SPEC_VERSION_MAJOR	0X15
#define FU_TPM_EVENTLOG_V2_HDR_IDX_SPEC_ERRATA		0x16
#define FU_TPM_EVENTLOG_V2_HDR_IDX_UINTN_SIZE		0x17
#define FU_TPM_EVENTLOG_V2_HDR_IDX_NUMBER_OF_ALGS	0x18

#define FU_TPM_EVENTLOG_V2_HDR_SIGNATURE		"Spec ID Event03"

#define FU_TPM_EVENTLOG_V2_IDX_PCR			0x00
#define FU_TPM_EVENTLOG_V2_IDX_TYPE			0x04
#define FU_TPM_EVENTLOG_V2_IDX_DIGEST_COUNT		0x08
#define FU_TPM_EVENTLOG_V2_SIZE				0x0c

static void
fu_tpm_eventlog_parser_item_free (FuTpmEventlogItem *item)
{
	g_bytes_unref (item->blob);
	if (item->checksum_sha1 != NULL)
		g_bytes_unref (item->checksum_sha1);
	if (item->checksum_sha256 != NULL)
		g_bytes_unref (item->checksum_sha256);
	g_free (item);
}

void
fu_tpm_eventlog_item_to_string (FuTpmEventlogItem *item, guint idt, GString *str)
{
	const gchar *tmp;
	g_autofree gchar *blobstr = fu_tpm_eventlog_blobstr (item->blob);
	g_autofree gchar *pcrstr = g_strdup_printf ("%s (%u)",
						    fu_tpm_eventlog_pcr_to_string (item->pcr),
						    item->pcr);
	fu_common_string_append_kv (str, idt, "PCR", pcrstr);
	fu_common_string_append_kx (str, idt, "Type", item->kind);
	tmp = fu_tpm_eventlog_item_kind_to_string (item->kind);
	if (tmp != NULL)
		fu_common_string_append_kv (str, idt, "Description", tmp);
	if (item->checksum_sha1 != NULL) {
		g_autofree gchar *csum = fu_tpm_eventlog_strhex (item->checksum_sha1);
		fu_common_string_append_kv (str, idt, "ChecksumSha1", csum);
	}
	if (item->checksum_sha256 != NULL) {
		g_autofree gchar *csum = fu_tpm_eventlog_strhex (item->checksum_sha256);
		fu_common_string_append_kv (str, idt, "ChecksumSha256", csum);
	}
	if (blobstr != NULL)
		fu_common_string_append_kv (str, idt, "BlobStr", blobstr);
}

static GPtrArray *
fu_tpm_eventlog_parser_parse_blob_v2 (const guint8 *buf, gsize bufsz,
				      FuTpmEventlogParserFlags flags,
				      GError **error)
{
	guint32 hdrsz = 0x0;
	g_autoptr(GPtrArray) items = NULL;

	/* advance over the header block */
	if (!fu_common_read_uint32_safe	(buf, bufsz,
					 FU_TPM_EVENTLOG_V1_IDX_EVENT_SIZE,
					 &hdrsz, G_LITTLE_ENDIAN, error))
		return NULL;
	items = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_tpm_eventlog_parser_item_free);
	for (gsize idx = FU_TPM_EVENTLOG_V1_SIZE + hdrsz; idx < bufsz;) {
		guint32 pcr = 0;
		guint32 event_type = 0;
		guint32 digestcnt = 0;
		guint32 datasz = 0;
		g_autoptr(GBytes) checksum_sha1 = NULL;
		g_autoptr(GBytes) checksum_sha256 = NULL;

		/* read entry */
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V2_IDX_PCR,
						 &pcr, G_LITTLE_ENDIAN, error))
			return NULL;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V2_IDX_TYPE,
						 &event_type, G_LITTLE_ENDIAN, error))
			return NULL;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V2_IDX_DIGEST_COUNT,
						 &digestcnt, G_LITTLE_ENDIAN, error))
			return NULL;

		/* read checksum block */
		idx += FU_TPM_EVENTLOG_V2_SIZE;
		for (guint i = 0; i < digestcnt; i++) {
			guint16 alg_type = 0;
			guint32 alg_size = 0;

			/* get checksum type */
			if (!fu_common_read_uint16_safe	(buf, bufsz, idx,
							 &alg_type, G_LITTLE_ENDIAN, error))
				return NULL;
			alg_size = fu_tpm_eventlog_hash_get_size (alg_type);
			if (alg_size == 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "hash algorithm 0x%x size not known",
					     alg_type);
				return NULL;
			}

			/* build checksum */
			idx += sizeof(alg_type);
			if (alg_type == TPM2_ALG_SHA1 ||
			    flags & FU_TPM_EVENTLOG_PARSER_FLAG_ALL_ALGS) {
				g_autofree guint8 *digest = g_malloc0 (alg_size);

				/* copy hash */
				if (!fu_memcpy_safe (digest, alg_size, 0x0,	/* dst */
						     buf, bufsz, idx,		/* src */
						     alg_size, error))
					return NULL;

				/* save this for analysis */
				if (alg_type == TPM2_ALG_SHA1)
					checksum_sha1 = g_bytes_new_take (g_steal_pointer (&digest), alg_size);
				else if (alg_type == TPM2_ALG_SHA256)
					checksum_sha1 = g_bytes_new_take (g_steal_pointer (&digest), alg_size);
			}

			/* next block */
			idx += alg_size;
		}

		/* read data block */
		if (!fu_common_read_uint32_safe	(buf, bufsz, idx,
						 &datasz, G_LITTLE_ENDIAN, error))
			return NULL;
		if (datasz > 1024 * 1024) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "event log item too large");
			return NULL;
		}

		/* save blob if PCR=0 */
		idx += sizeof(datasz);
		if (pcr == ESYS_TR_PCR0 ||
		    flags & FU_TPM_EVENTLOG_PARSER_FLAG_ALL_PCRS) {
			FuTpmEventlogItem *item;
			g_autofree guint8 *data = NULL;

			/* build item */
			data = g_malloc0 (datasz);
			if (!fu_memcpy_safe (data, datasz, 0x0,		/* dst */
					     buf, bufsz, idx, datasz,	/* src */
					     error))
				return NULL;

			/* not normally required */
			if (g_getenv ("FWUPD_TPM_EVENTLOG_VERBOSE") != NULL) {
				fu_common_dump_full (G_LOG_DOMAIN, "Event Data",
						     data, datasz, 20,
						     FU_DUMP_FLAGS_SHOW_ASCII);
			}
			item = g_new0 (FuTpmEventlogItem, 1);
			item->pcr = pcr;
			item->kind = event_type;
			item->checksum_sha1 = g_steal_pointer (&checksum_sha1);
			item->checksum_sha256 = g_steal_pointer (&checksum_sha256);
			item->blob = g_bytes_new_take (g_steal_pointer (&data), datasz);
			g_ptr_array_add (items, item);
		}

		/* next entry */
		idx += datasz;
	}

	/* success */
	return g_steal_pointer (&items);
}

GPtrArray *
fu_tpm_eventlog_parser_new (const guint8 *buf, gsize bufsz,
			    FuTpmEventlogParserFlags flags,
			    GError **error)
{
	gchar sig[] = FU_TPM_EVENTLOG_V2_HDR_SIGNATURE;
	g_autoptr(GPtrArray) items = NULL;

	g_return_val_if_fail (buf != NULL, NULL);

	/* look for TCG v2 signature */
	if (!fu_memcpy_safe ((guint8 *) sig, sizeof(sig), 0x0,		/* dst */
			     buf, bufsz, FU_TPM_EVENTLOG_V1_SIZE,	/* src */
			     sizeof(sig), error))
		return NULL;
	if (g_strcmp0 (sig, FU_TPM_EVENTLOG_V2_HDR_SIGNATURE) == 0)
		return fu_tpm_eventlog_parser_parse_blob_v2 (buf, bufsz, flags, error);

	/* assume v1 structure */
	items = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_tpm_eventlog_parser_item_free);
	for (gsize idx = 0; idx < bufsz; idx += FU_TPM_EVENTLOG_V1_SIZE) {
		guint32 datasz = 0;
		guint32 pcr = 0;
		guint32 event_type = 0;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V1_IDX_PCR,
						 &pcr, G_LITTLE_ENDIAN, error))
			return NULL;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V1_IDX_TYPE,
						 &event_type, G_LITTLE_ENDIAN, error))
			return NULL;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V1_IDX_EVENT_SIZE,
						 &datasz, G_LITTLE_ENDIAN, error))
			return NULL;
		if (datasz > 1024 * 1024) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "event log item too large");
			return NULL;
		}
		if (pcr == ESYS_TR_PCR0 ||
		    flags & FU_TPM_EVENTLOG_PARSER_FLAG_ALL_PCRS) {
			FuTpmEventlogItem *item;
			guint8 digest[TPM2_SHA1_DIGEST_SIZE] = { 0x0 };
			g_autofree guint8 *data = NULL;

			/* copy hash */
			if (!fu_memcpy_safe (digest, sizeof(digest), 0x0,			/* dst */
					     buf, bufsz, idx + FU_TPM_EVENTLOG_V1_IDX_DIGEST,	/* src */
					     sizeof(digest), error))
				return NULL;

			/* build item */
			data = g_malloc0 (datasz);
			if (!fu_memcpy_safe (data, datasz, 0x0,					/* dst */
					     buf, bufsz, idx + FU_TPM_EVENTLOG_V1_SIZE,		/* src */
					     datasz, error))
				return NULL;
			item = g_new0 (FuTpmEventlogItem, 1);
			item->pcr = pcr;
			item->kind = event_type;
			item->checksum_sha1 = g_bytes_new (digest, sizeof(digest));
			item->blob = g_bytes_new_take (g_steal_pointer (&data), datasz);
			g_ptr_array_add (items, item);

			/* not normally required */
			if (g_getenv ("FWUPD_TPM_EVENTLOG_VERBOSE") != NULL)
				fu_common_dump_bytes (G_LOG_DOMAIN, "Event Data", item->blob);
		}
		idx += datasz;
	}
	return g_steal_pointer (&items);

}
