/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuTpmEventlog"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-input-stream.h"
#include "fu-tpm-eventlog-item.h"
#include "fu-tpm-eventlog-v1.h"
#include "fu-tpm-struct.h"

struct _FuTpmEventlogV1 {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuTpmEventlogV1, fu_tpm_eventlog_v1, FU_TYPE_TPM_EVENTLOG)

static gboolean
fu_tpm_eventlog_v1_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	gsize streamsz = 0;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (gsize idx = 0; idx < streamsz; idx += FU_STRUCT_TPM_EVENT_LOG1_ITEM_SIZE) {
		guint32 datasz = 0;
		guint32 pcr = 0;
		guint32 event_type = 0;
		gsize digestsz = 0;
		const guint8 *digest;
		g_autoptr(FuStructTpmEventLog1Item) st = NULL;
		g_autoptr(FuTpmEventlogItem) item = fu_tpm_eventlog_item_new();
		g_autoptr(GBytes) checksum_sha1 = NULL;

		st = fu_struct_tpm_event_log1_item_parse_stream(stream, idx, error);
		if (st == NULL)
			return FALSE;
		pcr = fu_struct_tpm_event_log1_item_get_pcr(st);
		event_type = fu_struct_tpm_event_log1_item_get_type(st);
		datasz = fu_struct_tpm_event_log1_item_get_datasz(st);
		if (datasz > 1024 * 1024) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "event log item too large");
			return FALSE;
		}

		/* build item */
		fu_tpm_eventlog_item_set_pcr(item, pcr);
		fu_tpm_eventlog_item_set_kind(item, event_type);
		digest = fu_struct_tpm_event_log1_item_get_digest(st, &digestsz);
		checksum_sha1 = g_bytes_new(digest, digestsz);
		fu_tpm_eventlog_item_add_checksum(item, FU_TPM_ALG_SHA1, checksum_sha1);
		if (datasz > 0) {
			g_autoptr(GBytes) blob = NULL;
			blob = fu_input_stream_read_bytes(stream,
							  idx + st->buf->len,
							  datasz,
							  NULL,
							  error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_set_bytes(FU_FIRMWARE(item), blob);
		}
		if (!fu_firmware_add_image(firmware, FU_FIRMWARE(item), error))
			return FALSE;
		idx += datasz;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_tpm_eventlog_v1_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) items = fu_firmware_get_images(firmware);

	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(items, i);
		g_autoptr(FuStructTpmEventLog1Item) st = fu_struct_tpm_event_log1_item_new();
		g_autoptr(GBytes) digest = NULL;
		g_autoptr(GBytes) blob = NULL;

		fu_struct_tpm_event_log1_item_set_pcr(st, fu_tpm_eventlog_item_get_pcr(item));
		fu_struct_tpm_event_log1_item_set_type(st, fu_tpm_eventlog_item_get_kind(item));

		digest = fu_tpm_eventlog_item_get_checksum(item, FU_TPM_ALG_SHA1, error);
		if (digest == NULL)
			return NULL;
		if (!fu_struct_tpm_event_log1_item_set_digest(st,
							      g_bytes_get_data(digest, NULL),
							      g_bytes_get_size(digest),
							      error))
			return NULL;
		blob = fu_firmware_get_bytes(FU_FIRMWARE(item), error);
		if (blob == NULL)
			return NULL;
		fu_struct_tpm_event_log1_item_set_datasz(st, g_bytes_get_size(blob));
		fu_byte_array_append_bytes(st->buf, blob);

		/* done, dump */
		g_byte_array_append(buf, st->buf->data, st->buf->len);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_tpm_eventlog_v1_class_init(FuTpmEventlogV1Class *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_tpm_eventlog_v1_parse;
	firmware_class->write = fu_tpm_eventlog_v1_write;
}

static void
fu_tpm_eventlog_v1_init(FuTpmEventlogV1 *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_TPM_EVENTLOG_ITEM);
}

/**
 * fu_tpm_eventlog_v1_new:
 *
 * Creates a new object to parse TPMv1 eventlog data.
 *
 * Returns: a #FuTpmEventlog
 *
 * Since: 2.1.1
 **/
FuTpmEventlog *
fu_tpm_eventlog_v1_new(void)
{
	FuTpmEventlogV1 *self;
	self = g_object_new(FU_TYPE_TPM_EVENTLOG_V1, NULL);
	return FU_TPM_EVENTLOG(self);
}
