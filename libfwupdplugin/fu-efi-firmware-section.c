/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-efi-common.h"
#include "fu-efi-firmware-common.h"
#include "fu-efi-firmware-section.h"
#include "fu-efi-firmware-volume.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"
#include "fu-lzma-common.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

/**
 * FuEfiFirmwareSection:
 *
 * A UEFI firmware section.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 type;
	gchar *user_interface;
} FuEfiFirmwareSectionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiFirmwareSection, fu_efi_firmware_section, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_firmware_section_get_instance_private(o))

static void
fu_efi_firmware_section_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);

	fu_xmlb_builder_insert_kx(bn, "type", priv->type);
	if (priv->user_interface != NULL)
		fu_xmlb_builder_insert_kv(bn, "user_interface", priv->user_interface);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv(bn,
					  "name",
					  fu_efi_guid_to_name(fu_firmware_get_id(firmware)));
		fu_xmlb_builder_insert_kv(bn,
					  "type_name",
					  fu_efi_section_type_to_string(priv->type));
	}
}

static gboolean
fu_efi_firmware_section_parse_volume_image(FuEfiFirmwareSection *self,
					   GInputStream *stream,
					   FwupdInstallFlags flags,
					   GError **error)
{
	g_autoptr(FuFirmware) img = fu_efi_firmware_volume_new();
	if (!fu_firmware_parse_stream(img,
				      stream,
				      0x0,
				      flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
				      error)) {
		return FALSE;
	}
	fu_firmware_add_image(FU_FIRMWARE(self), img);
	return TRUE;
}

static gboolean
fu_efi_firmware_section_parse_lzma_sections(FuEfiFirmwareSection *self,
					    GInputStream *stream,
					    FwupdInstallFlags flags,
					    GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_uncomp = NULL;
	g_autoptr(GInputStream) stream_uncomp = NULL;

	/* parse all sections */
	blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, error);
	if (blob == NULL)
		return FALSE;
	blob_uncomp = fu_lzma_decompress_bytes(blob, error);
	if (blob_uncomp == NULL) {
		g_prefix_error(error, "failed to decompress: ");
		return FALSE;
	}
	stream_uncomp = g_memory_input_stream_new_from_bytes(blob_uncomp);
	if (!fu_efi_firmware_parse_sections(FU_FIRMWARE(self), stream_uncomp, 0, flags, error)) {
		g_prefix_error(error, "failed to parse sections: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_efi_firmware_section_parse_user_interface(FuEfiFirmwareSection *self,
					     GInputStream *stream,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = NULL;

	if (priv->user_interface != NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "UI already set as %s for section",
			    priv->user_interface);
		return FALSE;
	}
	buf = fu_input_stream_read_byte_array(stream, 0x0, G_MAXSIZE, error);
	if (buf == NULL)
		return FALSE;
	priv->user_interface = fu_utf16_to_utf8_byte_array(buf, G_LITTLE_ENDIAN, error);
	if (priv->user_interface == NULL)
		return FALSE;
	return TRUE;
}

static gboolean
fu_efi_firmware_section_parse_version(FuEfiFirmwareSection *self,
				      GInputStream *stream,
				      FwupdInstallFlags flags,
				      GError **error)
{
	guint16 version_raw = 0;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_input_stream_read_u16(stream, 0x0, &version_raw, G_LITTLE_ENDIAN, error)) {
		g_prefix_error(error, "failed to read raw version: ");
		return FALSE;
	}
	fu_firmware_set_version_raw(FU_FIRMWARE(self), version_raw);
	buf = fu_input_stream_read_byte_array(stream, sizeof(guint16), G_MAXSIZE, error);
	if (buf == NULL) {
		g_prefix_error(error, "failed to read version buffer: ");
		return FALSE;
	}
	version = fu_utf16_to_utf8_byte_array(buf, G_LITTLE_ENDIAN, error);
	if (version == NULL) {
		g_prefix_error(error, "failed to convert to UTF-16: ");
		return FALSE;
	}
	fu_firmware_set_version(FU_FIRMWARE(self), version);
	return TRUE;
}

static gboolean
fu_efi_firmware_section_parse_compression_sections(FuEfiFirmwareSection *self,
						   GInputStream *stream,
						   FwupdInstallFlags flags,
						   GError **error)
{
	g_autoptr(GByteArray) st = NULL;
	st = fu_struct_efi_section_compression_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_efi_section_compression_get_compression_type(st) ==
	    FU_EFI_COMPRESSION_TYPE_NOT_COMPRESSED) {
		if (!fu_efi_firmware_parse_sections(FU_FIRMWARE(self),
						    stream,
						    st->len,
						    flags,
						    error)) {
			g_prefix_error(error, "failed to parse sections: ");
			return FALSE;
		}
	} else {
		g_autoptr(FuFirmware) lz77_decompressor = fu_efi_lz77_decompressor_new();
		g_autoptr(GInputStream) lz77_stream = NULL;
		if (!fu_firmware_parse_stream(lz77_decompressor, stream, st->len, flags, error))
			return FALSE;
		lz77_stream = fu_firmware_get_stream(lz77_decompressor, error);
		if (lz77_stream == NULL)
			return FALSE;
		if (!fu_efi_firmware_parse_sections(FU_FIRMWARE(self),
						    lz77_stream,
						    0,
						    flags,
						    error)) {
			g_prefix_error(error, "failed to parse sections: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_efi_firmware_section_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	guint32 size;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* parse */
	st = fu_struct_efi_section_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* use extended size */
	if (fu_struct_efi_section_get_size(st) == 0xFFFFFF) {
		g_byte_array_unref(st);
		st = fu_struct_efi_section2_parse_stream(stream, offset, error);
		if (st == NULL)
			return FALSE;
		size = fu_struct_efi_section2_get_extended_size(st);
	} else {
		size = fu_struct_efi_section_get_size(st);
	}
	if (size < FU_STRUCT_EFI_SECTION_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid section size, got 0x%x",
			    (guint)size);
		return FALSE;
	}

	/* name */
	priv->type = fu_struct_efi_section_get_type(st);
	if (priv->type == FU_EFI_SECTION_TYPE_GUID_DEFINED) {
		g_autofree gchar *guid_str = NULL;
		g_autoptr(GByteArray) st_def = NULL;
		st_def = fu_struct_efi_section_guid_defined_parse_stream(stream, st->len, error);
		if (st_def == NULL)
			return FALSE;
		guid_str = fwupd_guid_to_string(fu_struct_efi_section_guid_defined_get_name(st_def),
						FWUPD_GUID_FLAG_MIXED_ENDIAN);
		fu_firmware_set_id(firmware, guid_str);
		if (fu_struct_efi_section_guid_defined_get_offset(st_def) < st_def->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid section size, got 0x%x",
				    (guint)fu_struct_efi_section_guid_defined_get_offset(st_def));
			return FALSE;
		}
		offset += fu_struct_efi_section_guid_defined_get_offset(st_def) - st->len;
	}

	/* create blob */
	offset += st->len;
	partial_stream = fu_partial_input_stream_new(stream, offset, size - offset);
	fu_firmware_set_offset(firmware, offset);
	fu_firmware_set_size(firmware, size);
	if (!fu_firmware_set_stream(firmware, partial_stream, error))
		return FALSE;

	/* nested volume */
	if (priv->type == FU_EFI_SECTION_TYPE_VOLUME_IMAGE) {
		if (!fu_efi_firmware_section_parse_volume_image(self,
								partial_stream,
								flags,
								error)) {
			g_prefix_error(error, "failed to parse nested volume: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_GUID_DEFINED &&
		   g_strcmp0(fu_firmware_get_id(firmware), FU_EFI_FIRMWARE_SECTION_LZMA_COMPRESS) ==
		       0) {
		if (!fu_efi_firmware_section_parse_lzma_sections(self,
								 partial_stream,
								 flags,
								 error)) {
			g_prefix_error(error, "failed to parse lzma section: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_USER_INTERFACE) {
		if (!fu_efi_firmware_section_parse_user_interface(self,
								  partial_stream,
								  flags,
								  error)) {
			g_prefix_error(error, "failed to parse user interface: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_VERSION) {
		if (!fu_efi_firmware_section_parse_version(self, partial_stream, flags, error)) {
			g_prefix_error(error, "failed to parse version: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_COMPRESSION) {
		if (!fu_efi_firmware_section_parse_compression_sections(self,
									partial_stream,
									flags,
									error)) {
			g_prefix_error(error, "failed to parse compression: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_firmware_section_write(FuFirmware *firmware, GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = fu_struct_efi_section_new();
	g_autoptr(GBytes) blob = NULL;

	/* simple blob for now */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;

	/* header */
	if (priv->type == FU_EFI_SECTION_TYPE_GUID_DEFINED) {
		fwupd_guid_t guid = {0x0};
		g_autoptr(GByteArray) st_def = fu_struct_efi_section_guid_defined_new();
		if (!fwupd_guid_from_string(fu_firmware_get_id(firmware),
					    &guid,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return NULL;
		fu_struct_efi_section_guid_defined_set_name(st_def, &guid);
		fu_struct_efi_section_guid_defined_set_offset(st_def, buf->len + st_def->len);
		g_byte_array_append(buf, st_def->data, st_def->len);
	}
	fu_struct_efi_section_set_type(buf, priv->type);
	fu_struct_efi_section_set_size(buf, buf->len + g_bytes_get_size(blob));

	/* blob */
	fu_byte_array_append_bytes(buf, blob);
	return g_steal_pointer(&buf);
}

static gboolean
fu_efi_firmware_section_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	const gchar *str;
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "type", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->type = tmp;
	str = xb_node_query_text(n, "user_interface", NULL);
	if (str != NULL) {
		if (priv->user_interface != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "UI already set as %s for section",
				    priv->user_interface);
			return FALSE;
		}
		priv->user_interface = g_strdup(str);
	}

	/* success */
	return TRUE;
}

static void
fu_efi_firmware_section_init(FuEfiFirmwareSection *self)
{
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	priv->type = FU_EFI_SECTION_TYPE_RAW;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	//	fu_firmware_set_alignment (FU_FIRMWARE (self), FU_FIRMWARE_ALIGNMENT_8);
}

static void
fu_efi_firmware_section_finalize(GObject *object)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(object);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	g_free(priv->user_interface);
	G_OBJECT_CLASS(fu_efi_firmware_section_parent_class)->finalize(object);
}

static void
fu_efi_firmware_section_class_init(FuEfiFirmwareSectionClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_efi_firmware_section_finalize;
	klass_firmware->parse = fu_efi_firmware_section_parse;
	klass_firmware->write = fu_efi_firmware_section_write;
	klass_firmware->build = fu_efi_firmware_section_build;
	klass_firmware->export = fu_efi_firmware_section_export;
}

/**
 * fu_efi_firmware_section_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.2
 **/
FuFirmware *
fu_efi_firmware_section_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_FIRMWARE_SECTION, NULL));
}
