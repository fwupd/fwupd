/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-cfu-firmware-struct.h"
#include "fu-cfu-payload.h"
#include "fu-common.h"
#include "fu-input-stream.h"

/**
 * FuCfuPayload:
 *
 * A CFU payload. This contains of a variable number of blocks, each containing the address, size
 * and the chunk data. The chunks do not have to be the same size, and the address ranges do not
 * have to be continuous.
 *
 * Documented: https://docs.microsoft.com/en-us/windows-hardware/drivers/cfu/cfu-specification
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuCfuPayload, fu_cfu_payload, FU_TYPE_FIRMWARE)

static gboolean
fu_cfu_payload_parse(FuFirmware *firmware,
		     GInputStream *stream,
		     FwupdInstallFlags flags,
		     GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;

	/* process into chunks */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		guint8 chunk_size = 0;
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GByteArray) st = NULL;

		st = fu_struct_cfu_payload_parse_stream(stream, offset, error);
		if (st == NULL)
			return FALSE;
		offset += st->len;
		chunk_size = fu_struct_cfu_payload_get_size(st);
		if (chunk_size == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "payload size was invalid");
			return FALSE;
		}
		blob = fu_input_stream_read_bytes(stream, offset, chunk_size, NULL, error);
		if (blob == NULL)
			return FALSE;
		chk = fu_chunk_bytes_new(blob);
		fu_chunk_set_address(chk, fu_struct_cfu_payload_get_addr(st));
		fu_firmware_add_chunk(firmware, chk);

		/* next! */
		offset += chunk_size;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_cfu_payload_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_firmware_get_chunks(firmware, error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GByteArray) st = fu_struct_cfu_payload_new();

		blob = fu_chunk_get_bytes(chk, error);
		if (blob == NULL)
			return NULL;
		fu_struct_cfu_payload_set_addr(st, fu_chunk_get_address(chk));
		fu_struct_cfu_payload_set_size(st, g_bytes_get_size(blob));
		g_byte_array_append(buf, st->data, st->len);
		fu_byte_array_append_bytes(buf, blob);
	}
	return g_steal_pointer(&buf);
}

static void
fu_cfu_payload_init(FuCfuPayload *self)
{
}

static void
fu_cfu_payload_class_init(FuCfuPayloadClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_cfu_payload_parse;
	firmware_class->write = fu_cfu_payload_write;
}

/**
 * fu_cfu_payload_new:
 *
 * Creates a new #FuFirmware for a CFU payload
 *
 * Since: 1.7.0
 **/
FuFirmware *
fu_cfu_payload_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CFU_PAYLOAD, NULL));
}
