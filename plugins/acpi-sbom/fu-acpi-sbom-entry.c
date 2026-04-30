/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-sbom-entry.h"

struct _FuAcpiSbomEntry {
	FuFirmware parent_instance;
	guint8 hdrver;
	FuUswidPayloadCompression compression;
	FuUswidPayloadFormat format;
};

G_DEFINE_TYPE(FuAcpiSbomEntry, fu_acpi_sbom_entry, FU_TYPE_FIRMWARE)

#define FU_ACPI_SBOM_FIRMWARE_MINIMUM_HDRVER 4

FuUswidPayloadFormat
fu_acpi_sbom_entry_get_format(FuAcpiSbomEntry *self)
{
	return self->format;
}

static void
fu_acpi_sbom_entry_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAcpiSbomEntry *self = FU_ACPI_SBOM_ENTRY(firmware);
	fu_xmlb_builder_insert_kx(bn, "hdrver", self->hdrver);
	if (self->compression != FU_USWID_PAYLOAD_COMPRESSION_NONE) {
		fu_xmlb_builder_insert_kv(
		    bn,
		    "compression",
		    fu_uswid_payload_compression_to_string(self->compression));
	}
	fu_xmlb_builder_insert_kv(bn, "format", fu_uswid_payload_format_to_string(self->format));
}

static gboolean
fu_acpi_sbom_entry_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuAcpiSbomEntry *self = FU_ACPI_SBOM_ENTRY(firmware);
	const gchar *str;
	guint64 tmp;

	/* optional hdrver */
	tmp = xb_node_query_text_as_uint(n, "hdrver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		self->hdrver = tmp;

	/* simple properties */
	str = xb_node_query_text(n, "format", error);
	if (str == NULL)
		return FALSE;
	self->format = fu_uswid_payload_format_from_string(str);

	/* optional */
	str = xb_node_query_text(n, "compression", NULL);
	if (str != NULL) {
		self->compression = fu_uswid_payload_compression_from_string(str);
		if (self->compression == FU_USWID_PAYLOAD_COMPRESSION_NONE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid compression type %s",
				    str);
			return FALSE;
		}
	} else {
		self->compression = FU_USWID_PAYLOAD_COMPRESSION_NONE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_acpi_sbom_entry_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuAcpiSbomEntry *self = FU_ACPI_SBOM_ENTRY(firmware);
	guint16 hdrsz;
	guint32 payloadsz;
	g_autoptr(FuStructAcpiSbomEntry) st = NULL;
	g_autoptr(GBytes) blob_uncompressed = NULL;

	/* TODO: parse firmware into images */
	st = fu_struct_acpi_sbom_entry_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* hdrver */
	self->hdrver = fu_struct_acpi_sbom_entry_get_hdrver(st);
	if (self->hdrver < FU_ACPI_SBOM_FIRMWARE_MINIMUM_HDRVER) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "header version was unsupported");
		return FALSE;
	}

	/* hdrsz+payloadsz */
	hdrsz = fu_struct_acpi_sbom_entry_get_hdrsz(st);
	if (hdrsz < st->buf->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "header size 0x%x is smaller than the fixed header",
			    hdrsz);
		return FALSE;
	}
	payloadsz = fu_struct_acpi_sbom_entry_get_payloadsz(st);
	if (payloadsz == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "payload size is invalid");
		return FALSE;
	}
	fu_firmware_set_size(firmware, hdrsz + payloadsz);
	self->compression = fu_struct_acpi_sbom_entry_get_compression(st);
	self->format = fu_struct_acpi_sbom_entry_get_format(st);

	/* zlib stream */
	if (self->compression == FU_USWID_PAYLOAD_COMPRESSION_ZLIB) {
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;
		conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB));
		istream1 = fu_partial_input_stream_new(stream, hdrsz, payloadsz, error);
		if (istream1 == NULL) {
			g_prefix_error_literal(error, "failed to cut SBOM payload: ");
			return FALSE;
		}
		if (!g_seekable_seek(G_SEEKABLE(istream1), 0, G_SEEK_SET, NULL, error))
			return FALSE;
		istream2 = g_converter_input_stream_new(istream1, conv);
		g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(istream2), FALSE);
		blob_uncompressed = fu_input_stream_read_bytes(istream2,
							       0,
							       fu_firmware_get_size_max(firmware),
							       NULL,
							       error);
		if (blob_uncompressed == NULL)
			return FALSE;
	} else if (self->compression == FU_USWID_PAYLOAD_COMPRESSION_LZMA) {
		g_autoptr(GBytes) payload_tmp = NULL;
		payload_tmp = fu_input_stream_read_bytes(stream, hdrsz, payloadsz, NULL, error);
		if (payload_tmp == NULL)
			return FALSE;
		blob_uncompressed = fu_lzma_decompress_bytes(payload_tmp, 16 * FU_MB, error);
		if (blob_uncompressed == NULL)
			return FALSE;
	} else if (self->compression == FU_USWID_PAYLOAD_COMPRESSION_NONE) {
		blob_uncompressed =
		    fu_input_stream_read_bytes(stream, hdrsz, payloadsz, NULL, error);
		if (blob_uncompressed == NULL)
			return FALSE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "compression format 0x%x is not supported",
			    self->compression);
		return FALSE;
	}

	/* payload */
	fu_firmware_set_bytes(firmware, blob_uncompressed);

	/* success */
	return TRUE;
}

static GByteArray *
fu_acpi_sbom_entry_write(FuFirmware *firmware, GError **error)
{
	FuAcpiSbomEntry *self = FU_ACPI_SBOM_ENTRY(firmware);
	g_autoptr(FuStructAcpiSbomEntry) st = fu_struct_acpi_sbom_entry_new();
	g_autoptr(GBytes) blob_uncompressed = NULL;
	g_autoptr(GBytes) blob_compressed = NULL;

	/* sanity check */
	if (self->hdrver > FU_STRUCT_USWID_DEFAULT_HDRVER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no idea how to write header format 0x%02x",
			    self->hdrver);
		return NULL;
	}

	/* generate early so we know the size */
	blob_uncompressed = fu_firmware_get_bytes(firmware, error);
	if (blob_uncompressed == NULL)
		return NULL;

	/* compression format */
	if (self->compression == FU_USWID_PAYLOAD_COMPRESSION_ZLIB) {
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;

		conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, -1));
		istream1 = g_memory_input_stream_new_from_bytes(blob_uncompressed);
		istream2 = g_converter_input_stream_new(istream1, conv);
		blob_compressed = fu_input_stream_read_bytes(istream2, 0, G_MAXSIZE, NULL, error);
		if (blob_compressed == NULL)
			return NULL;
	} else if (self->compression == FU_USWID_PAYLOAD_COMPRESSION_LZMA) {
		blob_compressed = fu_lzma_compress_bytes(blob_uncompressed, error);
		if (blob_compressed == NULL)
			return NULL;
	} else {
		blob_compressed = g_bytes_ref(blob_uncompressed);
	}

	/* pack */
	fu_struct_acpi_sbom_entry_set_hdrver(st, self->hdrver);
	fu_struct_acpi_sbom_entry_set_payloadsz(st, g_bytes_get_size(blob_compressed));
	fu_struct_acpi_sbom_entry_set_compression(st, self->compression);
	fu_struct_acpi_sbom_entry_set_format(st, self->format);
	fu_struct_acpi_sbom_entry_set_hdrsz(st, st->buf->len);

	/* success */
	fu_byte_array_append_bytes(st->buf, blob_compressed);
	return g_steal_pointer(&st->buf);
}

static void
fu_acpi_sbom_entry_init(FuAcpiSbomEntry *self)
{
	self->hdrver = FU_STRUCT_ACPI_SBOM_ENTRY_DEFAULT_HDRVER;
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2000);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 16 * FU_MB);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_acpi_sbom_entry_class_init(FuAcpiSbomEntryClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_acpi_sbom_entry_parse;
	firmware_class->write = fu_acpi_sbom_entry_write;
	firmware_class->build = fu_acpi_sbom_entry_build;
	firmware_class->export = fu_acpi_sbom_entry_export;
}

FuFirmware *
fu_acpi_sbom_entry_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_SBOM_ENTRY, NULL));
}
