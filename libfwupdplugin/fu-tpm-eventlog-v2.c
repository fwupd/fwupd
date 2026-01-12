/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuTpmEventlog"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-tpm-eventlog-common.h"
#include "fu-tpm-eventlog-item.h"
#include "fu-tpm-eventlog-v2.h"
#include "fu-tpm-struct.h"

/**
 * FuTpmEventlogV2:
 *
 * Parse the TPM eventlog.
 */

struct _FuTpmEventlogV2 {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuTpmEventlogV2, fu_tpm_eventlog_v2, FU_TYPE_TPM_EVENTLOG)

static guint32
fu_tpm_eventlog_v2_hash_get_size(FuTpmAlg hash_kind)
{
	if (hash_kind == FU_TPM_ALG_SHA1)
		return FU_TPM_DIGEST_SIZE_SHA1;
	if (hash_kind == FU_TPM_ALG_SHA256)
		return FU_TPM_DIGEST_SIZE_SHA256;
	if (hash_kind == FU_TPM_ALG_SHA384)
		return FU_TPM_DIGEST_SIZE_SHA384;
	if (hash_kind == FU_TPM_ALG_SHA512)
		return FU_TPM_DIGEST_SIZE_SHA512;
	if (hash_kind == FU_TPM_ALG_SM3_256)
		return FU_TPM_DIGEST_SIZE_SM3_256;
	return 0;
}

static gboolean
fu_tpm_eventlog_v2_parse_item(FuTpmEventlogV2 *self,
			      GInputStream *stream,
			      gsize *idx,
			      GError **error)
{
	guint32 pcr;
	guint32 digestcnt;
	guint32 datasz = 0;
	g_autoptr(GBytes) checksum_sha1 = NULL;
	g_autoptr(GBytes) checksum_sha256 = NULL;
	g_autoptr(GBytes) checksum_sha384 = NULL;
	g_autoptr(FuStructTpmEventLog2) st = NULL;
	g_autoptr(FuTpmEventlogItem) item = NULL;

	/* read checksum block */
	st = fu_struct_tpm_event_log2_parse_stream(stream, *idx, error);
	if (st == NULL)
		return FALSE;
	*idx += st->buf->len;
	digestcnt = fu_struct_tpm_event_log2_get_digest_count(st);
	for (guint i = 0; i < digestcnt; i++) {
		guint16 alg_type = 0;
		guint32 alg_size = 0;
		g_autofree guint8 *digest = NULL;
		g_autoptr(GBytes) checksum = NULL;

		/* get checksum type */
		if (!fu_input_stream_read_u16(stream, *idx, &alg_type, G_LITTLE_ENDIAN, error))
			return FALSE;
		alg_size = fu_tpm_eventlog_v2_hash_get_size(alg_type);
		if (alg_size == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "hash algorithm 0x%x size not known",
				    alg_type);
			return FALSE;
		}

		/* build checksum */
		*idx += sizeof(alg_type);

		/* copy hash */
		checksum = fu_input_stream_read_bytes(stream, *idx, alg_size, NULL, error);
		if (checksum == NULL)
			return FALSE;

		/* save this for analysis */
		if (alg_type == FU_TPM_ALG_SHA1)
			checksum_sha1 = g_bytes_ref(checksum);
		else if (alg_type == FU_TPM_ALG_SHA256)
			checksum_sha256 = g_bytes_ref(checksum);
		else if (alg_type == FU_TPM_ALG_SHA384)
			checksum_sha384 = g_bytes_ref(checksum);

		/* next block */
		*idx += alg_size;
	}

	/* read data block */
	if (!fu_input_stream_read_u32(stream, *idx, &datasz, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (datasz > 1024 * 1024) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "event log item too large");
		return FALSE;
	}

	/* save blob */
	*idx += sizeof(datasz);
	pcr = fu_struct_tpm_event_log2_get_pcr(st);

	/* build item */
	item = fu_tpm_eventlog_item_new();
	fu_tpm_eventlog_item_set_pcr(item, pcr);
	fu_tpm_eventlog_item_set_kind(item, fu_struct_tpm_event_log2_get_type(st));
	if (checksum_sha1 != NULL)
		fu_tpm_eventlog_item_add_checksum(item, FU_TPM_ALG_SHA1, checksum_sha1);
	if (checksum_sha256 != NULL)
		fu_tpm_eventlog_item_add_checksum(item, FU_TPM_ALG_SHA256, checksum_sha256);
	if (checksum_sha384 != NULL)
		fu_tpm_eventlog_item_add_checksum(item, FU_TPM_ALG_SHA384, checksum_sha384);
	if (datasz > 0) {
		g_autoptr(GBytes) blob = NULL;
		blob = fu_input_stream_read_bytes(stream, *idx, datasz, NULL, error);
		if (blob == NULL)
			return FALSE;
		fu_firmware_set_bytes(FU_FIRMWARE(item), blob);
	}
	if (!fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(item), error))
		return FALSE;

	/* next entry */
	*idx += datasz;
	return TRUE;
}

static gboolean
fu_tpm_eventlog_v2_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuTpmEventlogV2 *self = FU_TPM_EVENTLOG_V2(firmware);
	guint32 hdrsz = 0x0;
	gsize streamsz = 0;
	g_autoptr(FuStructTpmEventLog2Hdr) st_hdr = NULL;

	/* look for TCG v2 signature */
	st_hdr = fu_struct_tpm_event_log2_hdr_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;

	/* advance over the header block */
	hdrsz = fu_struct_tpm_event_log2_hdr_get_datasz(st_hdr);
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (gsize idx = FU_STRUCT_TPM_EVENT_LOG1_ITEM_SIZE + hdrsz; idx < streamsz;) {
		if (!fu_tpm_eventlog_v2_parse_item(self, stream, &idx, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_tpm_eventlog_v2_write_item(FuTpmEventlogItem *item, GError **error)
{
	guint32 digest_count = 0;
	g_autoptr(FuStructTpmEventLog2) st = fu_struct_tpm_event_log2_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) digest_sha1 = NULL;
	g_autoptr(GBytes) digest_sha256 = NULL;
	g_autoptr(GBytes) digest_sha384 = NULL;

	/* get all the checksums we know about */
	digest_sha1 = fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA1, NULL);
	if (digest_sha1 != NULL)
		digest_count++;
	digest_sha256 = fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA256, NULL);
	if (digest_sha256 != NULL)
		digest_count++;
	digest_sha384 = fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA384, NULL);
	if (digest_sha384 != NULL)
		digest_count++;

	/* write struct */
	fu_struct_tpm_event_log2_set_pcr(st, fu_tpm_eventlog_item_get_pcr(item));
	fu_struct_tpm_event_log2_set_type(st, fu_tpm_eventlog_item_get_kind(item));
	fu_struct_tpm_event_log2_set_digest_count(st, digest_count);
	if (digest_sha1 != NULL) {
		fu_byte_array_append_uint16(st->buf, FU_TPM_ALG_SHA1, G_LITTLE_ENDIAN);
		fu_byte_array_append_bytes(st->buf, digest_sha1);
	}
	if (digest_sha256 != NULL) {
		fu_byte_array_append_uint16(st->buf, FU_TPM_ALG_SHA256, G_LITTLE_ENDIAN);
		fu_byte_array_append_bytes(st->buf, digest_sha256);
	}
	if (digest_sha384 != NULL) {
		fu_byte_array_append_uint16(st->buf, FU_TPM_ALG_SHA384, G_LITTLE_ENDIAN);
		fu_byte_array_append_bytes(st->buf, digest_sha384);
	}

	/* write data */
	blob = fu_firmware_get_bytes(FU_FIRMWARE(item), error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_uint32(st->buf, g_bytes_get_size(blob), G_LITTLE_ENDIAN);
	fu_byte_array_append_bytes(st->buf, blob);

	/* done, dump */
	return g_steal_pointer(&st->buf);
}

static GByteArray *
fu_tpm_eventlog_v2_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) items = fu_firmware_get_images(firmware);
	g_autoptr(FuStructTpmEventLog2Hdr) st_hdr = fu_struct_tpm_event_log2_hdr_new();

	/* header */
	g_byte_array_append(buf, st_hdr->buf->data, st_hdr->buf->len);

	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(items, i);
		g_autoptr(GByteArray) buf_item = NULL;
		buf_item = fu_tpm_eventlog_v2_write_item(item, error);
		if (buf_item == NULL)
			return NULL;
		g_byte_array_append(buf, buf_item->data, buf_item->len);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_tpm_eventlog_v2_class_init(FuTpmEventlogV2Class *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_tpm_eventlog_v2_parse;
	firmware_class->write = fu_tpm_eventlog_v2_write;
}

static void
fu_tpm_eventlog_v2_init(FuTpmEventlogV2 *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_TPM_EVENTLOG_ITEM);
}

/**
 * fu_tpm_eventlog_v2_new:
 *
 * Creates a new object to parse TPMv2 eventlog data.
 *
 * Returns: a #FuTpmEventlog
 *
 * Since: 2.1.1
 **/
FuTpmEventlog *
fu_tpm_eventlog_v2_new(void)
{
	FuTpmEventlogV2 *self;
	self = g_object_new(FU_TYPE_TPM_EVENTLOG_V2, NULL);
	return FU_TPM_EVENTLOG(self);
}
