/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-weida-raw-firmware.h"
#include "fu-weida-raw-struct.h"

struct _FuWeidaRawFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuWeidaRawFirmware, fu_weida_raw_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_weida_raw_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	g_autoptr(FuWeidaRiffHeader) st_he = NULL;
	g_autoptr(FuWeidaChunkHeader) st_hed1 = NULL;

	st_he = fu_weida_riff_header_parse_stream(stream, offset, error);
	if (st_he == NULL)
		return FALSE;
	offset += FU_WEIDA_RIFF_HEADER_SIZE;

	st_hed1 = fu_weida_chunk_header_parse_stream(stream, offset, error);
	if (st_hed1 == NULL)
		return FALSE;
	offset += fu_weida_chunk_header_get_size(st_hed1);

	/* parse all sections */
	while (offset < fu_weida_riff_header_get_file_size(st_he)) {
		g_autoptr(FuWeidaChunkWif) st_wif = NULL;
		g_autoptr(GInputStream) partial_stream = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		st_wif = fu_weida_chunk_wif_parse_stream(stream, offset, error);
		if (st_wif == NULL)
			return FALSE;
		if (fu_weida_chunk_wif_get_fourcc(st_wif) != FU_WEIDA_RAW_FIRMWARE_FOURCC_FRWR &&
		    fu_weida_chunk_wif_get_fourcc(st_wif) != FU_WEIDA_RAW_FIRMWARE_FOURCC_CNFG) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "not FRWR or CNFG");
			return FALSE;
		}
		partial_stream =
		    fu_partial_input_stream_new(stream,
						offset + st_wif->len,
						fu_weida_chunk_wif_get_spi_size(st_wif),
						error);
		if (partial_stream == NULL)
			return FALSE;
		fu_firmware_set_offset(img, offset);
		fu_firmware_set_addr(img, fu_weida_chunk_wif_get_address(st_wif));
		fu_firmware_set_id(
		    img,
		    fu_weida_raw_firmware_fourcc_to_string(fu_weida_chunk_wif_get_fourcc(st_wif)));
		if (!fu_firmware_set_stream(img, partial_stream, error))
			return FALSE;
		fu_firmware_add_image(firmware, img);

		offset += fu_weida_chunk_wif_get_size(st_wif) + FU_WEIDA_CHUNK_WIF_OFFSET_ADDRESS;
	}

	/* success */
	return TRUE;
}

static void
fu_weida_raw_firmware_init(FuWeidaRawFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
}

static void
fu_weida_raw_firmware_class_init(FuWeidaRawFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_weida_raw_firmware_parse;
}
