/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-fw-struct.h"
#include "fu-pxi-tp-section.h"
#include "fu-pxi-tp-struct.h"

struct _FuPxiTpSection {
	FuFirmware parent_instance;

	FuPxiTpUpdateType update_type;
	guint8 update_info;
	guint32 flags; /* FuPxiTpFirmwareFlag bitmask */

	guint32 target_flash_start;
	guint32 internal_file_start;
	guint32 section_length;
	guint32 section_crc;

	guint8 reserved[PXI_TP_S_RESERVED_LEN];
	gchar external_file_name[PXI_TP_S_EXTNAME_LEN + 1];
};

G_DEFINE_TYPE(FuPxiTpSection, fu_pxi_tp_section, FU_TYPE_FIRMWARE)

static void
fu_pxi_tp_section_update_flags(FuPxiTpSection *self)
{
	guint32 flags = 0;

	if ((self->update_info & FU_PXI_TP_FIRMWARE_FLAG_VALID) != 0)
		flags |= FU_PXI_TP_FIRMWARE_FLAG_VALID;
	if ((self->update_info & FU_PXI_TP_FIRMWARE_FLAG_IS_EXTERNAL) != 0)
		flags |= FU_PXI_TP_FIRMWARE_FLAG_IS_EXTERNAL;

	self->flags = flags;
}

FuPxiTpSection *
fu_pxi_tp_section_new(void)
{
	return g_object_new(FU_TYPE_PXI_TP_SECTION, NULL);
}

gboolean
fu_pxi_tp_section_process_descriptor(FuPxiTpSection *self,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	g_autoptr(FuStructPxiTpFirmwareSectionHdr) st = NULL;
	const guint8 *reserved_src = NULL;
	const guint8 *name_src = NULL;
	gsize reserved_len = 0;
	gsize name_len = 0;
	gsize copy_len = 0;

	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);

	if (bufsz < FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "section descriptor too small: %" G_GSIZE_FORMAT,
			    bufsz);
		return FALSE;
	}

	/* section header do rustgen parse，offset = 0 */
	st = fu_struct_pxi_tp_firmware_section_hdr_parse(buf, bufsz, 0, error);
	if (st == NULL)
		return FALSE;

	/* core fields */
	self->update_type = fu_struct_pxi_tp_firmware_section_hdr_get_update_type(st);
	self->update_info = fu_struct_pxi_tp_firmware_section_hdr_get_update_info(st);
	self->target_flash_start = fu_struct_pxi_tp_firmware_section_hdr_get_target_flash_start(st);
	self->internal_file_start =
	    fu_struct_pxi_tp_firmware_section_hdr_get_internal_file_start(st);
	self->section_length = fu_struct_pxi_tp_firmware_section_hdr_get_section_length(st);
	self->section_crc = fu_struct_pxi_tp_firmware_section_hdr_get_section_crc(st);

	/* flags from update_info bitfield */
	fu_pxi_tp_section_update_flags(self);

	/* reserved[] */
	memset(self->reserved, 0, sizeof self->reserved);
	reserved_src = fu_struct_pxi_tp_firmware_section_hdr_get_shared(st, &reserved_len);
	if (reserved_src != NULL && reserved_len > 0) {
		copy_len = MIN((gsize)PXI_TP_S_RESERVED_LEN, reserved_len);
		if (!fu_memcpy_safe(self->reserved,
				    sizeof self->reserved,
				    0, /* dst offset */
				    reserved_src,
				    reserved_len,
				    0, /* src offset */
				    copy_len,
				    error)) {
			return FALSE;
		}
		if (copy_len < (gsize)PXI_TP_S_RESERVED_LEN) {
			memset(self->reserved + copy_len,
			       0,
			       (gsize)PXI_TP_S_RESERVED_LEN - copy_len);
		}
	}

	/* external_file_name[] */
	memset(self->external_file_name, 0, sizeof self->external_file_name);
	name_src = fu_struct_pxi_tp_firmware_section_hdr_get_extname(st, &name_len);
	if (name_src != NULL && name_len > 0) {
		copy_len = MIN((gsize)PXI_TP_S_EXTNAME_LEN, name_len);
		if (!fu_memcpy_safe((guint8 *)self->external_file_name,
				    sizeof self->external_file_name,
				    0,
				    name_src,
				    name_len,
				    0,
				    copy_len,
				    error)) {
			return FALSE;
		}
		if (copy_len >= (gsize)PXI_TP_S_EXTNAME_LEN)
			self->external_file_name[PXI_TP_S_EXTNAME_LEN] = '\0';
		else
			self->external_file_name[copy_len] = '\0';
	}

	return TRUE;
}

FuPxiTpUpdateType
fu_pxi_tp_section_get_update_type(FuPxiTpSection *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), 0);
	return self->update_type;
}

gboolean
fu_pxi_tp_section_has_flag(FuPxiTpSection *self, FuPxiTpFirmwareFlag flag)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), FALSE);
	return (self->flags & (guint32)flag) != 0;
}

guint32
fu_pxi_tp_section_get_target_flash_start(FuPxiTpSection *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), 0);
	return self->target_flash_start;
}

guint32
fu_pxi_tp_section_get_section_length(FuPxiTpSection *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), 0);
	return self->section_length;
}

guint32
fu_pxi_tp_section_get_section_crc(FuPxiTpSection *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), 0);
	return self->section_crc;
}

const guint8 *
fu_pxi_tp_section_get_reserved(FuPxiTpSection *self, gsize *len_out)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), NULL);
	if (len_out != NULL)
		*len_out = (gsize)PXI_TP_S_RESERVED_LEN;
	return self->reserved;
}

gboolean
fu_pxi_tp_section_attach_payload_stream(FuPxiTpSection *self,
					GInputStream *stream,
					gsize file_size,
					GError **error)
{
	guint32 internal_file_start = 0;
	guint32 section_length = 0;
	g_autoptr(GInputStream) substream = NULL;
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	internal_file_start = self->internal_file_start;
	section_length = self->section_length;
	if (section_length == 0)
		return TRUE;
	if ((guint64)internal_file_start + (guint64)section_length > (guint64)file_size) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "section payload out of range (off=0x%08x len=0x%08x, file=%" G_GSIZE_FORMAT
		    ")",
		    internal_file_start,
		    section_length,
		    file_size);
		return FALSE;
	}
	substream = fu_partial_input_stream_new(stream,
						(goffset)internal_file_start,
						(gsize)section_length,
						error);
	if (substream == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(FU_FIRMWARE(self), substream, error))
		return FALSE;
	return TRUE;
}

GByteArray *
fu_pxi_tp_section_get_payload(FuPxiTpSection *self, GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	const guint8 *buf = NULL;
	gsize bufsz = 0;
	gsize copy_len = 0;
	GByteArray *array = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), NULL);

	bytes = fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
	if (bytes == NULL)
		return NULL;

	buf = g_bytes_get_data(bytes, &bufsz);
	if (buf == NULL || bufsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "section payload is empty");
		return NULL;
	}

	copy_len = bufsz;
	if (self->section_length > 0 && self->section_length < copy_len)
		copy_len = self->section_length;

	array = g_byte_array_sized_new(copy_len);
	g_byte_array_append(array, buf, copy_len);
	return array;
}

static void
fu_pxi_tp_section_init(FuPxiTpSection *self)
{
	memset(self->reserved, 0, sizeof self->reserved);
	memset(self->external_file_name, 0, sizeof self->external_file_name);
	self->flags = 0;
}

static void
fu_pxi_tp_section_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiTpSection *self = FU_PXI_TP_SECTION(firmware);

	fu_xmlb_builder_insert_kx(bn, "update_type", self->update_type);
	fu_xmlb_builder_insert_kv(bn,
				  "update_type_name",
				  fu_pxi_tp_update_type_to_string(self->update_type));
	fu_xmlb_builder_insert_kx(bn, "update_info", self->update_info);
	fu_xmlb_builder_insert_kv(
	    bn,
	    "is_valid",
	    fu_pxi_tp_section_has_flag(self, FU_PXI_TP_FIRMWARE_FLAG_VALID) ? "true" : "false");
	fu_xmlb_builder_insert_kv(
	    bn,
	    "is_external",
	    fu_pxi_tp_section_has_flag(self, FU_PXI_TP_FIRMWARE_FLAG_IS_EXTERNAL) ? "true"
										  : "false");
	fu_xmlb_builder_insert_kx(bn, "target_flash_start", self->target_flash_start);
	fu_xmlb_builder_insert_kx(bn, "internal_file_start", self->internal_file_start);
	fu_xmlb_builder_insert_kx(bn, "section_length", self->section_length);
	fu_xmlb_builder_insert_kx(bn, "section_crc", self->section_crc);

	/* reserved bytes as hex */
	if (sizeof self->reserved > 0) {
		g_autofree gchar *rhex = fu_pxi_tp_common_hexdump_slice(self->reserved,
									sizeof self->reserved,
									sizeof self->reserved);
		fu_xmlb_builder_insert_kv(bn, "reserved_hex", rhex);
	}

	fu_xmlb_builder_insert_kv(bn,
				  "external_file",
				  self->external_file_name[0] != '\0' ? self->external_file_name
								      : "");
}

static void
fu_pxi_tp_section_class_init(FuPxiTpSectionClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->export = fu_pxi_tp_section_export;
}
