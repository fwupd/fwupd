/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-block.h"

struct _FuIlitekItsBlock {
	FuFirmware parent_instance;
	guint16 crc;
};

G_DEFINE_TYPE(FuIlitekItsBlock, fu_ilitek_its_block, FU_TYPE_FIRMWARE)

static void
fu_ilitek_its_block_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIlitekItsBlock *self = FU_ILITEK_ITS_BLOCK(firmware);
	fu_xmlb_builder_insert_kx(bn, "crc", self->crc);
}

static gboolean
fu_ilitek_its_block_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuIlitekItsBlock *self = FU_ILITEK_ITS_BLOCK(firmware);
	gsize streamsz = 0;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* calculate CRC of block minus the CRC16 itself */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	partial_stream = fu_partial_input_stream_new(stream, 0x0, streamsz - 2, error);
	if (partial_stream == NULL)
		return FALSE;
	if (!fu_input_stream_compute_crc16(partial_stream,
					   FU_CRC_KIND_B16_KERMIT,
					   &self->crc,
					   error))
		return FALSE;

	/* success */
	return TRUE;
}

guint16
fu_ilitek_its_block_get_crc(FuIlitekItsBlock *self)
{
	return self->crc;
}

static void
fu_ilitek_its_block_init(FuIlitekItsBlock *self)
{
}

static void
fu_ilitek_its_block_class_init(FuIlitekItsBlockClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->export = fu_ilitek_its_block_export;
	firmware_class->parse = fu_ilitek_its_block_parse;
}

FuFirmware *
fu_ilitek_its_block_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ILITEK_ITS_BLOCK, NULL));
}
