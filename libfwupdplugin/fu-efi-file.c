/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-file.h"
#include "fu-efi-section.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-sum.h"

/**
 * FuEfiFile:
 *
 * A UEFI file.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 type;
	guint8 attrib;
} FuEfiFilePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiFile, fu_efi_file, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_file_get_instance_private(o))

#define FU_EFI_FILE_SIZE_MAX 0x1000000 /* 16 MB */

static void
fu_efi_file_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiFile *self = FU_EFI_FILE(firmware);
	FuEfiFilePrivate *priv = GET_PRIVATE(self);

	fu_xmlb_builder_insert_kx(bn, "attrib", priv->attrib);
	fu_xmlb_builder_insert_kx(bn, "type", priv->type);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv(bn,
					  "name",
					  fu_efi_guid_to_name(fu_firmware_get_id(firmware)));
		fu_xmlb_builder_insert_kv(bn, "type_name", fu_efi_file_type_to_string(priv->type));
	}
}

static guint8
fu_efi_file_hdr_checksum8(GBytes *blob)
{
	gsize bufsz = 0;
	guint8 checksum = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);
	for (gsize i = 0; i < bufsz; i++) {
		if (i == FU_STRUCT_EFI_FILE_OFFSET_HDR_CHECKSUM)
			continue;
		if (i == FU_STRUCT_EFI_FILE_OFFSET_DATA_CHECKSUM)
			continue;
		if (i == FU_STRUCT_EFI_FILE_OFFSET_STATE)
			continue;
		checksum += buf[i];
	}
	return (guint8)(0x100u - (guint)checksum);
}

static gboolean
fu_efi_file_parse(FuFirmware *firmware,
		  GInputStream *stream,
		  FuFirmwareParseFlags flags,
		  GError **error)
{
	FuEfiFile *self = FU_EFI_FILE(firmware);
	FuEfiFilePrivate *priv = GET_PRIVATE(self);
	guint32 size = 0x0;
	g_autofree gchar *guid_str = NULL;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* parse */
	st = fu_struct_efi_file_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	priv->type = fu_struct_efi_file_get_type(st);
	priv->attrib = fu_struct_efi_file_get_attrs(st);
	guid_str =
	    fwupd_guid_to_string(fu_struct_efi_file_get_name(st), FWUPD_GUID_FLAG_MIXED_ENDIAN);
	fu_firmware_set_id(firmware, guid_str);

	/* extended size exists so size must be set to zero */
	if (priv->attrib & FU_EFI_FILE_ATTRIB_LARGE_FILE) {
		if (fu_struct_efi_file_get_size(st) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid FFS size -- expected 0x0 and got 0x%x",
				    (guint)fu_struct_efi_file_get_size(st));
			return FALSE;
		}
		fu_struct_efi_file_unref(st);
		st = fu_struct_efi_file2_parse_stream(stream, 0x0, error);
		if (st == NULL)
			return FALSE;
		size = fu_struct_efi_file2_get_extended_size(st);
	} else {
		size = fu_struct_efi_file_get_size(st);
	}
	if (size < st->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid FFS length, got 0x%x",
			    (guint)size);
		return FALSE;
	}

	/* verify header checksum */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 hdr_checksum_verify;
		g_autoptr(GBytes) hdr_blob = NULL;

		hdr_blob = fu_input_stream_read_bytes(stream, 0x0, st->len, NULL, error);
		if (hdr_blob == NULL)
			return FALSE;
		hdr_checksum_verify = fu_efi_file_hdr_checksum8(hdr_blob);
		if (hdr_checksum_verify != fu_struct_efi_file_get_hdr_checksum(st)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum invalid, got %02x, expected %02x",
				    hdr_checksum_verify,
				    fu_struct_efi_file_get_hdr_checksum(st));
			return FALSE;
		}
	}

	/* add simple blob */
	partial_stream = fu_partial_input_stream_new(stream, st->len, size - st->len, error);
	if (partial_stream == NULL) {
		g_prefix_error(error, "failed to cut EFI blob: ");
		return FALSE;
	}

	/* verify data checksum */
	if ((priv->attrib & FU_EFI_FILE_ATTRIB_CHECKSUM) > 0 &&
	    (flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 data_checksum_verify = 0;
		if (!fu_input_stream_compute_sum8(partial_stream, &data_checksum_verify, error))
			return FALSE;
		if (0x100 - data_checksum_verify != fu_struct_efi_file_get_data_checksum(st)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum invalid, got 0x%02x, expected 0x%02x",
				    0x100u - data_checksum_verify,
				    fu_struct_efi_file_get_data_checksum(st));
			return FALSE;
		}
	}

	/* add sections */
	if (priv->type != FU_EFI_FILE_TYPE_FFS_PAD && priv->type != FU_EFI_FILE_TYPE_RAW) {
		if (!fu_efi_parse_sections(firmware, partial_stream, 0, flags, error)) {
			g_prefix_error(error, "failed to add firmware image: ");
			return FALSE;
		}
	} else {
		if (!fu_firmware_set_stream(firmware, partial_stream, error))
			return FALSE;
	}

	/* align size for volume */
	fu_firmware_set_size(firmware,
			     fu_common_align_up(size, fu_firmware_get_alignment(firmware)));

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_file_write_sections(FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* sanity check */
	if (fu_firmware_get_alignment(firmware) > FU_FIRMWARE_ALIGNMENT_1M) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "alignment invalid, got 0x%02x",
			    fu_firmware_get_alignment(firmware));
		return NULL;
	}

	/* no sections defined */
	if (images->len == 0)
		return fu_firmware_get_bytes_with_patches(firmware, error);

	/* add each section */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = NULL;
		fu_firmware_set_offset(img, buf->len);
		blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
		fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_4, 0xFF);

		/* sanity check */
		if (buf->len > FU_EFI_FILE_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "EFI file too large, 0x%02x > 0x%02x",
				    (guint)buf->len,
				    (guint)FU_EFI_FILE_SIZE_MAX);
			return NULL;
		}
	}

	/* success */
	return g_bytes_new(buf->data, buf->len);
}

static GByteArray *
fu_efi_file_write(FuFirmware *firmware, GError **error)
{
	FuEfiFile *self = FU_EFI_FILE(firmware);
	FuEfiFilePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t guid = {0x0};
	g_autoptr(GByteArray) st = fu_struct_efi_file_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) hdr_blob = NULL;

	/* simple blob for now */
	blob = fu_efi_file_write_sections(firmware, error);
	if (blob == NULL)
		return NULL;
	if (fu_firmware_get_id(firmware) != NULL) {
		if (!fwupd_guid_from_string(fu_firmware_get_id(firmware),
					    &guid,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return NULL;
	}
	fu_struct_efi_file_set_name(st, &guid);
	fu_struct_efi_file_set_hdr_checksum(st, 0x0);
	fu_struct_efi_file_set_data_checksum(st, 0x100 - fu_sum8_bytes(blob));
	fu_struct_efi_file_set_type(st, priv->type);
	fu_struct_efi_file_set_attrs(st, priv->attrib);
	fu_struct_efi_file_set_size(st, g_bytes_get_size(blob) + st->len);

	/* fix up header checksum */
	hdr_blob = g_bytes_new_static(st->data, st->len);
	fu_struct_efi_file_set_hdr_checksum(st, fu_efi_file_hdr_checksum8(hdr_blob));

	/* success */
	fu_byte_array_append_bytes(st, blob);
	return g_steal_pointer(&st);
}

static gboolean
fu_efi_file_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiFile *self = FU_EFI_FILE(firmware);
	FuEfiFilePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "type", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->type = tmp;
	tmp = xb_node_query_text_as_uint(n, "attrib", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->attrib = tmp;

	/* success */
	return TRUE;
}

static void
fu_efi_file_init(FuEfiFile *self)
{
	FuEfiFilePrivate *priv = GET_PRIVATE(self);
	priv->attrib = FU_EFI_FILE_ATTRIB_NONE;
	priv->type = FU_EFI_FILE_TYPE_RAW;
	fu_firmware_set_alignment(FU_FIRMWARE(self), FU_FIRMWARE_ALIGNMENT_8);
	g_type_ensure(FU_TYPE_EFI_SECTION);
}

static void
fu_efi_file_class_init(FuEfiFileClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_efi_file_parse;
	firmware_class->write = fu_efi_file_write;
	firmware_class->build = fu_efi_file_build;
	firmware_class->export = fu_efi_file_export;
}

/**
 * fu_efi_file_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 2.0.0
 **/
FuFirmware *
fu_efi_file_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_FILE, NULL));
}
