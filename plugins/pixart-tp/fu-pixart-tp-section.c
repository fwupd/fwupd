/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-section.h"
#include "fu-pixart-tp-struct.h"

struct _FuPixartTpSection {
	FuFirmware parent_instance;
	FuPixartTpUpdateType update_type;
	FuPixartTpFirmwareFlags flags;
	guint32 target_flash_start;
	guint32 section_crc;
	GByteArray *reserved; /* nullable */
};

G_DEFINE_TYPE(FuPixartTpSection, fu_pixart_tp_section, FU_TYPE_FIRMWARE)

FuPixartTpSection *
fu_pixart_tp_section_new(void)
{
	return g_object_new(FU_TYPE_PIXART_TP_SECTION, NULL);
}

FuPixartTpUpdateType
fu_pixart_tp_section_get_update_type(FuPixartTpSection *self)
{
	g_return_val_if_fail(FU_IS_PIXART_TP_SECTION(self), 0);
	return self->update_type;
}

gboolean
fu_pixart_tp_section_has_flag(FuPixartTpSection *self, FuPixartTpFirmwareFlags flag)
{
	g_return_val_if_fail(FU_IS_PIXART_TP_SECTION(self), FALSE);
	return (self->flags & flag) != 0;
}

guint32
fu_pixart_tp_section_get_target_flash_start(FuPixartTpSection *self)
{
	g_return_val_if_fail(FU_IS_PIXART_TP_SECTION(self), 0);
	return self->target_flash_start;
}

guint32
fu_pixart_tp_section_get_crc(FuPixartTpSection *self)
{
	g_return_val_if_fail(FU_IS_PIXART_TP_SECTION(self), 0);
	return self->section_crc;
}

GByteArray *
fu_pixart_tp_section_get_reserved(FuPixartTpSection *self)
{
	g_return_val_if_fail(FU_IS_PIXART_TP_SECTION(self), NULL);
	return g_byte_array_ref(self->reserved);
}

static void
fu_pixart_tp_section_init(FuPixartTpSection *self)
{
	self->reserved =
	    g_byte_array_sized_new(FU_STRUCT_PIXART_TP_FIRMWARE_SECTION_HDR_N_ELEMENTS_SHARED);
}

static void
fu_pixart_tp_section_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPixartTpSection *self = FU_PIXART_TP_SECTION(firmware);

	fu_xmlb_builder_insert_kv(bn,
				  "update_type",
				  fu_pixart_tp_update_type_to_string(self->update_type));
	if (self->flags != FU_PIXART_TP_FIRMWARE_FLAG_NONE) {
		g_autofree gchar *str = fu_pixart_tp_firmware_flags_to_string(self->flags);
		fu_xmlb_builder_insert_kv(bn, "flags", str);
	}
	fu_xmlb_builder_insert_kx(bn, "target_flash_start", self->target_flash_start);
	fu_xmlb_builder_insert_kx(bn, "section_crc", self->section_crc);
	if (self->reserved != NULL) {
		g_autofree gchar *str = fu_byte_array_to_string(self->reserved);
		fu_xmlb_builder_insert_kv(bn, "reserved", str);
	}
}

static gboolean
fu_pixart_tp_section_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuPixartTpSection *self = FU_PIXART_TP_SECTION(firmware);
	const gchar *tmp;
	guint64 tmp64 = 0;

	/* simple properties */
	tmp = xb_node_query_text(n, "update_type", NULL);
	if (tmp != NULL)
		self->update_type = fu_pixart_tp_update_type_from_string(tmp);
	tmp = xb_node_query_text(n, "flags", NULL);
	if (tmp != NULL)
		self->flags = fu_pixart_tp_firmware_flags_from_string(tmp);
	tmp = xb_node_query_text(n, "target_flash_start", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->target_flash_start = (guint32)tmp64;
	}
	tmp = xb_node_query_text(n, "section_crc", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->section_crc = (guint32)tmp64;
	}
	tmp = xb_node_query_text(n, "reserved", NULL);
	if (tmp != NULL) {
		if (self->reserved != NULL)
			g_byte_array_unref(self->reserved);
		self->reserved = fu_byte_array_from_string(tmp, error);
		if (self->reserved == NULL)
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_section_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   gsize offset,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	FuPixartTpSection *self = FU_PIXART_TP_SECTION(firmware);
	const guint8 *reserved_src = NULL;
	gsize reserved_len = 0;
	guint32 section_length;
	g_autofree gchar *extname = NULL;
	g_autoptr(FuStructPixartTpFirmwareSectionHdr) st = NULL;

	g_return_val_if_fail(FU_IS_PIXART_TP_SECTION(self), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);

	st = fu_struct_pixart_tp_firmware_section_hdr_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* core fields */
	self->update_type = fu_struct_pixart_tp_firmware_section_hdr_get_update_type(st);
	self->flags = fu_struct_pixart_tp_firmware_section_hdr_get_update_info(st);
	self->target_flash_start =
	    fu_struct_pixart_tp_firmware_section_hdr_get_target_flash_start(st);
	fu_firmware_set_offset(
	    firmware,
	    fu_struct_pixart_tp_firmware_section_hdr_get_internal_file_start(st));
	section_length = fu_struct_pixart_tp_firmware_section_hdr_get_section_length(st);
	self->section_crc = fu_struct_pixart_tp_firmware_section_hdr_get_section_crc(st);

	/* reserved */
	reserved_src = fu_struct_pixart_tp_firmware_section_hdr_get_shared(st, &reserved_len);
	g_byte_array_append(self->reserved, reserved_src, reserved_len);

	/* extname */
	extname = fu_struct_pixart_tp_firmware_section_hdr_get_extname(st);
	if (extname != NULL)
		fu_firmware_set_filename(FU_FIRMWARE(self), extname);

	/* data */
	if (section_length != 0) {
		g_autoptr(GInputStream) partial_stream = NULL;
		partial_stream = fu_partial_input_stream_new(stream,
							     fu_firmware_get_offset(firmware),
							     section_length,
							     error);
		if (partial_stream == NULL)
			return FALSE;
		if (!fu_firmware_set_stream(FU_FIRMWARE(self), partial_stream, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_pixart_tp_section_write(FuFirmware *firmware, GError **error)
{
	FuPixartTpSection *self = FU_PIXART_TP_SECTION(firmware);
	g_autoptr(FuStructPixartTpFirmwareSectionHdr) st =
	    fu_struct_pixart_tp_firmware_section_hdr_new();

	fu_struct_pixart_tp_firmware_section_hdr_set_update_type(st, self->update_type);
	fu_struct_pixart_tp_firmware_section_hdr_set_update_info(st, self->flags);
	fu_struct_pixart_tp_firmware_section_hdr_set_target_flash_start(st,
									self->target_flash_start);
	fu_struct_pixart_tp_firmware_section_hdr_set_internal_file_start(
	    st,
	    fu_firmware_get_offset(firmware));
	fu_struct_pixart_tp_firmware_section_hdr_set_section_length(st,
								    fu_firmware_get_size(firmware));
	fu_struct_pixart_tp_firmware_section_hdr_set_section_crc(st, self->section_crc);
	if (self->reserved != NULL) {
		if (!fu_struct_pixart_tp_firmware_section_hdr_set_shared(st,
									 self->reserved->data,
									 self->reserved->len,
									 error))
			return NULL;
	}
	if (fu_firmware_get_filename(firmware) != NULL) {
		if (!fu_struct_pixart_tp_firmware_section_hdr_set_extname(
			st,
			fu_firmware_get_filename(firmware),
			error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&st->buf);
}

static void
fu_pixart_tp_section_finalize(GObject *obj)
{
	FuPixartTpSection *self = FU_PIXART_TP_SECTION(obj);
	if (self->reserved != NULL)
		g_byte_array_unref(self->reserved);
	G_OBJECT_CLASS(fu_pixart_tp_section_parent_class)->finalize(obj);
}

static void
fu_pixart_tp_section_class_init(FuPixartTpSectionClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	firmware_class->export = fu_pixart_tp_section_export;
	firmware_class->parse_full = fu_pixart_tp_section_parse;
	firmware_class->build = fu_pixart_tp_section_build;
	firmware_class->write = fu_pixart_tp_section_write;
	object_class->finalize = fu_pixart_tp_section_finalize;
}
