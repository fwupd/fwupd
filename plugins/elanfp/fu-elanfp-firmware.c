/*
 * Copyright 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elanfp-firmware.h"
#include "fu-elanfp-struct.h"

struct _FuElanfpFirmware {
	FuFirmwareClass parent_instance;
	guint32 format_version;
};

G_DEFINE_TYPE(FuElanfpFirmware, fu_elanfp_firmware, FU_TYPE_FIRMWARE)

static void
fu_elanfp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "format_version", self->format_version);
}

static gboolean
fu_elanfp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "format_version", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->format_version = tmp;

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	return fu_struct_elanfp_firmware_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_elanfp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	gsize offset = 0;

	/* file format version */
	if (!fu_input_stream_read_u32(stream,
				      offset + 0x4,
				      &self->format_version,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	/* read indexes */
	offset += 0x10;
	while (1) {
		guint32 start_addr = 0;
		guint32 length = 0;
		guint32 fwtype = 0;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GInputStream) stream_tmp = NULL;

		/* type, reserved, start-addr, len */
		if (!fu_input_stream_read_u32(stream,
					      offset + 0x0,
					      &fwtype,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;

		/* check not already added */
		img = fu_firmware_get_image_by_idx(firmware, fwtype, NULL);
		if (img != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "already parsed image with fwtype 0x%x",
				    fwtype);
			return FALSE;
		}

		/* done */
		if (fwtype == FU_ELANTP_FIRMWARE_IDX_END)
			break;
		switch (fwtype) {
		case FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_A:
		case FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_B:
			img = fu_cfu_offer_new();
			break;
		case FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_A:
		case FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_B:
			img = fu_cfu_payload_new();
			break;
		default:
			img = fu_firmware_new();
			break;
		}
		fu_firmware_set_idx(img, fwtype);
		if (!fu_input_stream_read_u32(stream,
					      offset + 0x8,
					      &start_addr,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;
		fu_firmware_set_addr(img, start_addr);
		if (!fu_input_stream_read_u32(stream,
					      offset + 0xC,
					      &length,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;
		if (length == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "zero size fwtype 0x%x not supported",
				    fwtype);
			return FALSE;
		}

		stream_tmp = fu_partial_input_stream_new(stream, start_addr, length, error);
		if (stream_tmp == NULL)
			return FALSE;
		if (!fu_firmware_parse_stream(img,
					      stream_tmp,
					      0x0,
					      flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
					      error))
			return FALSE;
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		offset += 0x10;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_elanfp_firmware_write(FuFirmware *firmware, GError **error)
{
	FuElanfpFirmware *self = FU_ELANFP_FIRMWARE(firmware);
	gsize offset = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* S2F_HEADER */
	fu_byte_array_append_uint32(buf, 0x46325354, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, self->format_version, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* ICID, assumed */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */

	/* S2F_INDEX */
	offset += 0x10 + ((imgs->len + 1) * 0x10);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_uint32(buf, fu_firmware_get_idx(img), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
		fu_byte_array_append_uint32(buf, offset, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, g_bytes_get_size(blob), G_LITTLE_ENDIAN);
		offset += g_bytes_get_size(blob);
	}

	/* end of index */
	fu_byte_array_append_uint32(buf, FU_ELANTP_FIRMWARE_IDX_END, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* assumed */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* assumed */

	/* data */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_elanfp_firmware_init(FuElanfpFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 256);
	g_type_ensure(FU_TYPE_CFU_OFFER);
	g_type_ensure(FU_TYPE_CFU_PAYLOAD);
}

static void
fu_elanfp_firmware_class_init(FuElanfpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_elanfp_firmware_validate;
	firmware_class->parse = fu_elanfp_firmware_parse;
	firmware_class->write = fu_elanfp_firmware_write;
	firmware_class->export = fu_elanfp_firmware_export;
	firmware_class->build = fu_elanfp_firmware_build;
}
