/*
 * Copyright 2026 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-gpu-pldm-firmware.h"
#include "fu-amd-gpu-pldm-struct.h"

struct _FuAmdGpuPldmFirmware {
	FuFirmware parent_instance;
	guint8 format_revision;
	guint16 component_bitmap_bit_length;
};

/**
 * FuAmdGpuPldmFirmware:
 *
 * A PLDM firmware update package, as defined by DMTF DSP0267.
 *
 * The package starts with a header (identified by a well-known UUID) that
 * describes the package version, a set of firmware device ID records (each
 * with a list of matching descriptors), and a component image information
 * area.  Each component image is exposed as a child firmware image located
 * at its `ComponentLocationOffset` within the package.
 */

G_DEFINE_TYPE(FuAmdGpuPldmFirmware, fu_amd_gpu_pldm_firmware, FU_TYPE_FIRMWARE)

static void
fu_amd_gpu_pldm_firmware_export(FuFirmware *firmware,
				FuFirmwareExportFlags flags,
				XbBuilderNode *bn)
{
	FuAmdGpuPldmFirmware *self = FU_AMD_GPU_PLDM_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "format_revision", self->format_revision);
	fu_xmlb_builder_insert_kx(bn,
				  "component_bitmap_bit_length",
				  self->component_bitmap_bit_length);
}

static gboolean
fu_amd_gpu_pldm_firmware_validate(FuFirmware *firmware,
				  FuInputStream *stream,
				  gsize offset,
				  GError **error)
{
	return fu_struct_amd_gpu_pldm_header_validate_stream(stream, offset, error);
}

/* read a PLDM version string; only the ASCII and UTF-8 types are decoded */
static gchar *
fu_amd_gpu_pldm_firmware_read_version(FuInputStream *stream,
				      FuAmdGpuPldmStringType kind,
				      gsize offset,
				      guint8 length,
				      GError **error)
{
	if (length == 0)
		return g_strdup("");
	if (kind != FU_AMD_GPU_PLDM_STRING_TYPE_ASCII && kind != FU_AMD_GPU_PLDM_STRING_TYPE_UTF8) {
		/* not fatal: we just cannot represent the string as UTF-8 */
		return g_strdup("");
	}
	return fu_input_stream_read_string(stream, offset, length, error);
}

static gboolean
fu_amd_gpu_pldm_firmware_verify_checksum(FuInputStream *stream, gsize header_size, GError **error)
{
	guint32 crc_actual;
	guint32 crc_expected = 0;
	g_autoptr(GBytes) blob = NULL;

	if (header_size <= sizeof(guint32)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "package header size too small for checksum");
		return FALSE;
	}
	blob = fu_input_stream_read_bytes(stream, 0x0, header_size - sizeof(guint32), NULL, error);
	if (blob == NULL)
		return FALSE;
	crc_actual = fu_crc32_bytes(FU_CRC_KIND_B32_STANDARD, blob);
	if (!fu_input_stream_read_u32(stream,
				      header_size - sizeof(guint32),
				      &crc_expected,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (crc_actual != crc_expected) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "package header checksum invalid, got 0x%08x, expected 0x%08x",
			    crc_actual,
			    crc_expected);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_amd_gpu_pldm_firmware_parse_device_records(FuAmdGpuPldmFirmware *self,
					      FuInputStream *stream,
					      gsize *offset,
					      GError **error)
{
	guint8 record_count = 0;
	gsize bitmap_sz = self->component_bitmap_bit_length / 8;

	if (!fu_input_stream_read_u8(stream, *offset, &record_count, error))
		return FALSE;
	if (!fu_size_checked_inc(offset, sizeof(guint8), error))
		return FALSE;

	for (guint i = 0; i < record_count; i++) {
		gsize desc_offset;
		gsize record_offset = *offset;
		guint16 record_length;
		guint8 descriptor_count;
		g_autoptr(FuStructAmdGpuPldmDeviceIdRecord) st_rec = NULL;

		st_rec = fu_struct_amd_gpu_pldm_device_id_record_parse_stream(stream,
									      record_offset,
									      error);
		if (st_rec == NULL)
			return FALSE;
		record_length = fu_struct_amd_gpu_pldm_device_id_record_get_record_length(st_rec);
		if (record_length < st_rec->buf->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "device ID record %u length 0x%x too small",
				    i,
				    record_length);
			return FALSE;
		}

		/* walk the descriptors so malformed records are rejected */
		descriptor_count =
		    fu_struct_amd_gpu_pldm_device_id_record_get_descriptor_count(st_rec);
		desc_offset = record_offset;
		if (!fu_size_checked_inc(&desc_offset, st_rec->buf->len, error))
			return FALSE;
		if (!fu_size_checked_inc(&desc_offset, bitmap_sz, error))
			return FALSE;
		if (!fu_size_checked_inc(
			&desc_offset,
			fu_struct_amd_gpu_pldm_device_id_record_get_version_string_length(st_rec),
			error))
			return FALSE;
		for (guint j = 0; j < descriptor_count; j++) {
			g_autoptr(FuStructAmdGpuPldmDescriptor) st_desc = NULL;
			st_desc = fu_struct_amd_gpu_pldm_descriptor_parse_stream(stream,
										 desc_offset,
										 error);
			if (st_desc == NULL) {
				g_prefix_error(error, "record %u descriptor %u: ", i, j);
				return FALSE;
			}
			if (!fu_size_checked_inc(&desc_offset, st_desc->buf->len, error))
				return FALSE;
			if (!fu_size_checked_inc(
				&desc_offset,
				fu_struct_amd_gpu_pldm_descriptor_get_descriptor_length(st_desc),
				error))
				return FALSE;
		}

		/* advance to the next record using the authoritative record length */
		if (!fu_size_checked_inc(offset, record_length, error)) {
			g_prefix_error(error, "record %u offset overflow: ", i);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_amd_gpu_pldm_firmware_parse_components(FuAmdGpuPldmFirmware *self,
					  FuInputStream *stream,
					  gsize *offset,
					  GError **error)
{
	guint16 component_count = 0;

	if (!fu_input_stream_read_u16(stream, *offset, &component_count, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_size_checked_inc(offset, sizeof(guint16), error))
		return FALSE;

	for (guint i = 0; i < component_count; i++) {
		guint32 loc;
		guint32 sz;
		FuAmdGpuPldmStringType ver_kind;
		guint8 ver_len;
		g_autofree gchar *version = NULL;
		g_autoptr(FuStructAmdGpuPldmComponent) st_comp = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(FuInputStream) partial = NULL;

		st_comp = fu_struct_amd_gpu_pldm_component_parse_stream(stream, *offset, error);
		if (st_comp == NULL)
			return FALSE;
		if (!fu_size_checked_inc(offset, st_comp->buf->len, error))
			return FALSE;

		/* the component version string trails the fixed structure */
		ver_kind = fu_struct_amd_gpu_pldm_component_get_version_string_type(st_comp);
		ver_len = fu_struct_amd_gpu_pldm_component_get_version_string_length(st_comp);
		version = fu_amd_gpu_pldm_firmware_read_version(stream,
								ver_kind,
								*offset,
								ver_len,
								error);
		if (version == NULL)
			return FALSE;
		if (!fu_size_checked_inc(offset, ver_len, error))
			return FALSE;

		fu_firmware_set_idx(img, fu_struct_amd_gpu_pldm_component_get_identifier(st_comp));
		if (version[0] != '\0')
			fu_firmware_set_version(img, version);

		/* the payload lives at ComponentLocationOffset within the package */
		loc = fu_struct_amd_gpu_pldm_component_get_location_offset(st_comp);
		sz = fu_struct_amd_gpu_pldm_component_get_size(st_comp);
		fu_firmware_set_addr(img, loc);
		partial = fu_partial_input_stream_new(stream, loc, sz, error);
		if (partial == NULL)
			return FALSE;
		if (!fu_firmware_set_stream(img, partial, error))
			return FALSE;
		if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_amd_gpu_pldm_firmware_parse(FuFirmware *firmware,
			       FuInputStream *stream,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	FuAmdGpuPldmFirmware *self = FU_AMD_GPU_PLDM_FIRMWARE(firmware);
	gsize offset;
	guint16 header_size;
	FuAmdGpuPldmStringType ver_kind;
	guint8 ver_len;
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructAmdGpuPldmHeader) st_hdr = NULL;

	/* package header information */
	st_hdr = fu_struct_amd_gpu_pldm_header_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	self->format_revision = fu_struct_amd_gpu_pldm_header_get_format_revision(st_hdr);
	self->component_bitmap_bit_length =
	    fu_struct_amd_gpu_pldm_header_get_component_bitmap_bit_length(st_hdr);
	header_size = fu_struct_amd_gpu_pldm_header_get_header_size(st_hdr);
	offset = st_hdr->buf->len;

	/* package version string trails the fixed header */
	ver_kind = fu_struct_amd_gpu_pldm_header_get_version_string_type(st_hdr);
	ver_len = fu_struct_amd_gpu_pldm_header_get_version_string_length(st_hdr);
	version = fu_amd_gpu_pldm_firmware_read_version(stream, ver_kind, offset, ver_len, error);
	if (version == NULL)
		return FALSE;
	if (version[0] != '\0')
		fu_firmware_set_version(firmware, version);
	if (!fu_size_checked_inc(&offset, ver_len, error))
		return FALSE;

	/* the component bitmap length must be a multiple of 8 bits */
	if (self->component_bitmap_bit_length % 8 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "component bitmap bit length 0x%x is not a multiple of 8",
			    self->component_bitmap_bit_length);
		return FALSE;
	}

	/* verify the CRC-32 over the whole header */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_amd_gpu_pldm_firmware_verify_checksum(stream, header_size, error))
			return FALSE;
	}

	/* firmware device identification area */
	if (!fu_amd_gpu_pldm_firmware_parse_device_records(self, stream, &offset, error))
		return FALSE;

	/* component image information area */
	if (!fu_amd_gpu_pldm_firmware_parse_components(self, stream, &offset, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_amd_gpu_pldm_firmware_init(FuAmdGpuPldmFirmware *self)
{
}

static void
fu_amd_gpu_pldm_firmware_class_init(FuAmdGpuPldmFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FIRMWARE);
	firmware_class->validate = fu_amd_gpu_pldm_firmware_validate;
	firmware_class->parse = fu_amd_gpu_pldm_firmware_parse;
	firmware_class->export = fu_amd_gpu_pldm_firmware_export;
	fu_firmware_set_size_max(firmware_class, 128 * FU_MB);
}

FuFirmware *
fu_amd_gpu_pldm_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_GPU_PLDM_FIRMWARE, NULL));
}
