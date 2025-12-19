/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydium-tp-image.h"

struct _FuRaydiumTpImage {
	FuFirmware parent_instance;
	guint32 checksum;
};

G_DEFINE_TYPE(FuRaydiumTpImage, fu_raydium_tp_image, FU_TYPE_FIRMWARE)

static void
fu_raydium_tp_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuRaydiumTpImage *self = FU_RAYDIUM_TP_IMAGE(firmware);
	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

guint32
fu_raydium_tp_image_get_checksum(FuRaydiumTpImage *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_IMAGE(self), 0);
	return self->checksum;
}

static gboolean
fu_raydium_tp_image_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuRaydiumTpImage *self = FU_RAYDIUM_TP_IMAGE(firmware);
	gsize streamsz = 0;
	guint32 crc_calc = G_MAXUINT32;
	g_autoptr(GInputStream) stream_crc = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "image too small");
		return FALSE;
	}
	stream_crc = fu_partial_input_stream_new(stream, 0, streamsz - 4, error);
	if (stream_crc == NULL)
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream_crc, FU_CRC_KIND_B32_MPEG2, &crc_calc, error))
		return FALSE;
	if (!fu_input_stream_read_u32(stream,
				      streamsz - 4,
				      &self->checksum,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0 && crc_calc != self->checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "CRC invalid, got 0x%x and expected 0x%x",
			    crc_calc,
			    self->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_raydium_tp_image_init(FuRaydiumTpImage *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_raydium_tp_image_class_init(FuRaydiumTpImageClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_raydium_tp_image_export;
	klass_firmware->parse = fu_raydium_tp_image_parse;
}
