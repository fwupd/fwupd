/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-cfu-payload.h"
#include "fu-common.h"
#include "fu-mem.h"

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
		     GBytes *fw,
		     gsize offset,
		     FwupdInstallFlags flags,
		     GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* process into chunks */
	while (offset < bufsz) {
		guint32 chunk_addr = 0;
		guint8 chunk_size = 0;
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* read chunk header */
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset,
					    &chunk_addr,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf, bufsz, offset + 0x4, &chunk_size, error))
			return FALSE;
		offset += 0x5;
		blob = fu_bytes_new_offset(fw, offset, chunk_size, error);
		if (blob == NULL)
			return FALSE;
		chk = fu_chunk_bytes_new(blob);
		fu_chunk_set_address(chk, chunk_addr);
		fu_firmware_add_chunk(firmware, chk);

		/* next! */
		offset += chunk_size;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_cfu_payload_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_firmware_get_chunks(firmware, error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		fu_byte_array_append_uint32(buf, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint8(buf, fu_chunk_get_data_sz(chk));
		g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	}
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_cfu_payload_init(FuCfuPayload *self)
{
}

static void
fu_cfu_payload_class_init(FuCfuPayloadClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_cfu_payload_parse;
	klass_firmware->write = fu_cfu_payload_write;
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
