/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-firmware.h"
#include "fu-focal-moc-struct.h"

struct _FuFocalMocFirmware {
	FuFirmware parent_instance;
	guint32 entry_offset;
	GBytes *digest;
	GBytes *signature;
};

G_DEFINE_TYPE(FuFocalMocFirmware, fu_focal_moc_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_focal_moc_firmware_validate(FuFirmware *firmware,
			       FuInputStream *stream,
			       gsize offset,
			       GError **error)
{
	return fu_struct_focal_moc_firmware_header_validate_stream(stream, offset, error);
}

static gboolean
fu_focal_moc_firmware_check_reserved(FuStructFocalMocFirmwareHeader *st, GError **error)
{
	const struct {
		gsize offset;
		gsize end;
	} regions[] = {
	    {FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_OFFSET_ENTRY_OFFSET + sizeof(guint32),
	     FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_OFFSET_DIGEST},
	    {FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_OFFSET_SIGNATURE +
		 FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE_SIGNATURE,
	     FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE},
	};

	/* the writer rebuilds these regions as zeros, so nonzero content would
	 * not survive a parse-write cycle */
	for (guint i = 0; i < G_N_ELEMENTS(regions); i++) {
		for (gsize off = regions[i].offset; off < regions[i].end; off++) {
			guint8 value = 0;
			if (!fu_memread_uint8_safe(st->buf->data, st->buf->len, off, &value, error))
				return FALSE;
			if (value != 0x0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "reserved header byte not zero at 0x%zx",
					    off);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_firmware_parse(FuFirmware *firmware,
			    FuInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuFocalMocFirmware *self = FU_FOCAL_MOC_FIRMWARE(firmware);
	gsize streamsz = 0;
	guint32 body_sz;
	guint32 version;
	g_autoptr(FuStructFocalMocFirmwareHeader) st = NULL;
	g_autoptr(FuInputStream) stream_body = NULL;

	st = fu_struct_focal_moc_firmware_header_parse_stream(stream, 0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_focal_moc_firmware_header_get_kind(st) != FU_FOCAL_MOC_FIRMWARE_KIND_APP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware type invalid: got=0x%x expected=0x%x",
			    fu_struct_focal_moc_firmware_header_get_kind(st),
			    (guint)FU_FOCAL_MOC_FIRMWARE_KIND_APP);
		return FALSE;
	}
	if (!fu_focal_moc_firmware_check_reserved(st, error))
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	body_sz = fu_struct_focal_moc_firmware_header_get_size(st);
	if (body_sz == 0 || streamsz != FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE + body_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size invalid: header=0x%x file=0x%zx expected=0x%x",
			    body_sz,
			    streamsz,
			    FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE + body_sz);
		return FALSE;
	}
	self->entry_offset = fu_struct_focal_moc_firmware_header_get_entry_offset(st);
	if (self->entry_offset >= body_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware entry offset invalid: offset=0x%x size=0x%x",
			    self->entry_offset,
			    body_sz);
		return FALSE;
	}
	version = fu_struct_focal_moc_firmware_header_get_version(st);
	if (version > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware version exceeds four hex digits: 0x%x",
			    version);
		return FALSE;
	}
	fu_firmware_set_version_raw(firmware, version);

	self->digest = g_bytes_new(fu_struct_focal_moc_firmware_header_get_digest(st, NULL),
				   FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE_DIGEST);
	self->signature = g_bytes_new(fu_struct_focal_moc_firmware_header_get_signature(st, NULL),
				      FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE_SIGNATURE);

	stream_body = fu_partial_input_stream_new(stream,
						  FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE,
						  body_sz,
						  error);
	if (stream_body == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware, stream_body, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_focal_moc_firmware_write(FuFirmware *firmware, GError **error)
{
	FuFocalMocFirmware *self = FU_FOCAL_MOC_FIRMWARE(firmware);
	guint64 version = 0;
	g_autoptr(FuStructFocalMocFirmwareHeader) st = fu_struct_focal_moc_firmware_header_new();
	g_autoptr(GBytes) blob = fu_firmware_get_bytes_with_patches(firmware, error);

	if (blob == NULL)
		return NULL;
	if (g_bytes_get_size(blob) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware body is empty");
		return NULL;
	}
	if (self->entry_offset >= g_bytes_get_size(blob)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware entry offset invalid: offset=0x%x size=0x%zx",
			    self->entry_offset,
			    g_bytes_get_size(blob));
		return NULL;
	}
	version = fu_firmware_get_version_raw(firmware);
	if (version > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware version exceeds four hex digits: 0x%" G_GINT64_MODIFIER "x",
			    version);
		return NULL;
	}
	fu_struct_focal_moc_firmware_header_set_kind(st, FU_FOCAL_MOC_FIRMWARE_KIND_APP);
	fu_struct_focal_moc_firmware_header_set_version(st, (guint32)version);
	fu_struct_focal_moc_firmware_header_set_size(st, (guint32)g_bytes_get_size(blob));
	fu_struct_focal_moc_firmware_header_set_entry_offset(st, self->entry_offset);
	if (self->digest != NULL) {
		if (!fu_struct_focal_moc_firmware_header_set_digest(
			st,
			g_bytes_get_data(self->digest, NULL),
			g_bytes_get_size(self->digest),
			error))
			return NULL;
	}
	if (self->signature != NULL) {
		if (!fu_struct_focal_moc_firmware_header_set_signature(
			st,
			g_bytes_get_data(self->signature, NULL),
			g_bytes_get_size(self->signature),
			error))
			return NULL;
	}
	fu_byte_array_append_bytes(st->buf, blob);

	/* success */
	return g_byte_array_ref(st->buf);
}

static void
fu_focal_moc_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFocalMocFirmware *self = FU_FOCAL_MOC_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kx(bn, "entry_offset", self->entry_offset);
	if (self->digest != NULL) {
		g_autofree gchar *str = fu_bytes_to_string(self->digest);
		fu_xmlb_builder_insert_kv(bn, "digest", str);
	}
	if (self->signature != NULL) {
		g_autofree gchar *str = fu_bytes_to_string(self->signature);
		fu_xmlb_builder_insert_kv(bn, "signature", str);
	}
}

static gboolean
fu_focal_moc_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuFocalMocFirmware *self = FU_FOCAL_MOC_FIRMWARE(firmware);
	const gchar *tmp;
	guint64 value;

	value = xb_node_query_text_as_uint(n, "entry_offset", NULL);
	if (value != G_MAXUINT64 && value <= G_MAXUINT32)
		self->entry_offset = (guint32)value;
	tmp = xb_node_query_text(n, "digest", NULL);
	if (tmp != NULL) {
		g_autoptr(GBytes) blob = fu_bytes_from_string(tmp, error);
		if (blob == NULL)
			return FALSE;
		if (g_bytes_get_size(blob) != FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE_DIGEST) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "digest size invalid: 0x%zx",
				    g_bytes_get_size(blob));
			return FALSE;
		}
		g_clear_pointer(&self->digest, g_bytes_unref);
		self->digest = g_steal_pointer(&blob);
	}
	tmp = xb_node_query_text(n, "signature", NULL);
	if (tmp != NULL) {
		g_autoptr(GBytes) blob = fu_bytes_from_string(tmp, error);
		if (blob == NULL)
			return FALSE;
		if (g_bytes_get_size(blob) != FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE_SIGNATURE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "signature size invalid: 0x%zx",
				    g_bytes_get_size(blob));
			return FALSE;
		}
		g_clear_pointer(&self->signature, g_bytes_unref);
		self->signature = g_steal_pointer(&blob);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_focal_moc_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return g_strdup_printf("%04" G_GINT64_MODIFIER "x", version_raw);
}

static void
fu_focal_moc_firmware_init(FuFocalMocFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PLAIN);
}

static void
fu_focal_moc_firmware_finalize(GObject *object)
{
	FuFocalMocFirmware *self = FU_FOCAL_MOC_FIRMWARE(object);

	g_clear_pointer(&self->digest, g_bytes_unref);
	g_clear_pointer(&self->signature, g_bytes_unref);
	G_OBJECT_CLASS(fu_focal_moc_firmware_parent_class)->finalize(object);
}

static void
fu_focal_moc_firmware_class_init(FuFocalMocFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_focal_moc_firmware_finalize;
	firmware_class->convert_version = fu_focal_moc_firmware_convert_version;
	firmware_class->validate = fu_focal_moc_firmware_validate;
	firmware_class->parse = fu_focal_moc_firmware_parse;
	firmware_class->write = fu_focal_moc_firmware_write;
	firmware_class->export = fu_focal_moc_firmware_export;
	firmware_class->build = fu_focal_moc_firmware_build;
	fu_firmware_set_size_max(firmware_class, FU_FOCAL_MOC_FIRMWARE_SIZE_MAX);
}

FuFirmware *
fu_focal_moc_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FOCAL_MOC_FIRMWARE, NULL));
}
