/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-section.h"
#include "fu-pxi-tp-struct.h"

struct _FuPxiTpSection {
	FuFirmware parent_instance;

	FuPxiTpUpdateType update_type;
	guint8 update_info;
	FuPxiTpFirmwareFlags flags;
	guint32 target_flash_start;
	guint32 internal_file_start;
	guint32 section_length;
	guint32 section_crc;

	GByteArray *reserved;
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

FuPxiTpUpdateType
fu_pxi_tp_section_get_update_type(FuPxiTpSection *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), 0);
	return self->update_type;
}

gboolean
fu_pxi_tp_section_has_flag(FuPxiTpSection *self, FuPxiTpFirmwareFlags flag)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), FALSE);
	return (self->flags & flag) != 0;
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

GByteArray *
fu_pxi_tp_section_get_reserved(FuPxiTpSection *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_SECTION(self), NULL);

	if (self->reserved == NULL)
		return NULL;

	return g_byte_array_ref(self->reserved);
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

	/* section header parsed by rustgen, offset = 0 */
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

	/* reserved */
	reserved_src = fu_struct_pxi_tp_firmware_section_hdr_get_shared(st, &reserved_len);
	if (reserved_src != NULL && reserved_len > 0) {
		copy_len = MIN((gsize)FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_SHARED,
			       reserved_len);
		if (!fu_memcpy_safe(self->reserved->data,
				    self->reserved->len,
				    0, /* dst offset */
				    reserved_src,
				    reserved_len,
				    0, /* src offset */
				    copy_len,
				    error)) {
			return FALSE;
		}
		/* clear tail if short */
		if (copy_len < (gsize)FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_SHARED) {
			memset(self->reserved->data + copy_len,
			       0,
			       (gsize)FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_SHARED -
				   copy_len);
		}
	} else {
		/* deterministic reset when missing */
		memset(self->reserved->data, 0, self->reserved->len);
	}

	/* extname -> baseclass filename (preferred by fwupdtool firmware-extract) */
	name_src = fu_struct_pxi_tp_firmware_section_hdr_get_extname(st, &name_len);
	if (name_src != NULL && name_len > 0) {
		g_autofree gchar *name = g_strndup(
		    (const gchar *)name_src,
		    MIN(name_len, (gsize)FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_EXTNAME));
		fu_firmware_set_filename(FU_FIRMWARE(self), name);
	} else {
		/* optional: clear filename if not provided */
		fu_firmware_set_filename(FU_FIRMWARE(self), NULL);
	}

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
	/* fixed-length reserved blob */
	self->reserved =
	    g_byte_array_sized_new(FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_SHARED);
	fu_byte_array_set_size(self->reserved,
			       FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_SHARED,
			       0x00);
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
	if (self->reserved != NULL) {
		g_autofree gchar *rhex = fu_byte_array_to_string(self->reserved);
		fu_xmlb_builder_insert_kv(bn, "reserved_hex", rhex);
	}
}

static gboolean
fu_pxi_tp_section_parse(FuFirmware *firmware,
			GInputStream *stream,
			FuFirmwareParseFlags flags,
			GError **error)
{
	FuPxiTpSection *self = FU_PXI_TP_SECTION(firmware);
	g_autoptr(GInputStream) substream = NULL;

	if (self->section_length == 0)
		return TRUE;

	substream = fu_partial_input_stream_new(stream,
						(goffset)self->internal_file_start,
						(gsize)self->section_length,
						error);
	if (substream == NULL)
		return FALSE;

	return fu_firmware_set_stream(FU_FIRMWARE(self), substream, error);
}

static void
fu_pxi_tp_section_finalize(GObject *obj)
{
	FuPxiTpSection *self = FU_PXI_TP_SECTION(obj);

	g_clear_pointer(&self->reserved, g_byte_array_unref);

	G_OBJECT_CLASS(fu_pxi_tp_section_parent_class)->finalize(obj);
}

static void
fu_pxi_tp_section_class_init(FuPxiTpSectionClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	firmware_class->export = fu_pxi_tp_section_export;
	firmware_class->parse = fu_pxi_tp_section_parse;
	object_class->finalize = fu_pxi_tp_section_finalize;
}
