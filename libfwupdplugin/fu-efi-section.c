/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiSection"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efi-section.h"
#include "fu-efi-struct.h"
#include "fu-efi-volume.h"
#include "fu-input-stream.h"
#include "fu-lzma-common.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

/**
 * FuEfiSection:
 *
 * A UEFI firmware section.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 type;
	gchar *user_interface;
} FuEfiSectionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiSection, fu_efi_section, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_section_get_instance_private(o))

static void
fu_efi_section_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiSection *self = FU_EFI_SECTION(firmware);
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);

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
fu_efi_section_parse_volume_image(FuEfiSection *self,
				  GInputStream *stream,
				  FwupdInstallFlags flags,
				  GError **error)
{
	g_autoptr(FuFirmware) img = fu_efi_volume_new();
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
fu_efi_section_parse_lzma_sections(FuEfiSection *self,
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
	if (!fu_efi_parse_sections(FU_FIRMWARE(self), stream_uncomp, 0, flags, error)) {
		g_prefix_error(error, "failed to parse sections: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_efi_section_parse_user_interface(FuEfiSection *self,
				    GInputStream *stream,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);
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
fu_efi_section_parse_version(FuEfiSection *self,
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
	fu_firmware_set_version(FU_FIRMWARE(self), version); /* nocheck:set-version */
	return TRUE;
}

static gboolean
fu_efi_section_parse_compression_sections(FuEfiSection *self,
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
		if (!fu_efi_parse_sections(FU_FIRMWARE(self), stream, st->len, flags, error)) {
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
		if (!fu_efi_parse_sections(FU_FIRMWARE(self), lz77_stream, 0, flags, error)) {
			g_prefix_error(error, "failed to parse sections: ");
			return FALSE;
		}
	}
	return TRUE;
}

static const gchar *
fu_efi_section_freeform_subtype_guid_to_string(const gchar *guid)
{
	struct {
		const gchar *guid;
		const gchar *str;
	} freeform_guids[] = {
	    {"00781ca1-5de3-405f-abb8-379c3c076984", "AmiRomLayoutGuid"},
	    {"20feebde-e739-420e-ae31-77e2876508c0", "IntelRstOprom"},
	    {"224d6eb4-307f-45ba-9dc3-fe9fc6b38148", "IntelEntRaidController"},
	    {"2ebe0275-6458-4af9-91ed-d3f4edb100aa", "SignOn"},
	    {"380b6b4f-1454-41f2-a6d3-61d1333e8cb4", "IntelGop"},
	    {"50339d20-c90a-4bb2-9aff-d8a11b23bc15", "I219?Oprom"},
	    {"88a15a4f-977d-4682-b17c-da1f316c1f32", "RomLayout"},
	    {"9bec7109-6d7a-413a-8e4b-019ced0503e1", "AmiBoardInfoSectionGuid"},
	    {"ab56dc60-0057-11da-a8db-000102eee626", "?BuildData"},
	    {"c5a4306e-e247-4ecd-a9d8-5b1985d3dcda", "?Oprom"},
	    {"c9352cc3-a354-44e5-8776-b2ed8dd781ec", "IntelEntRaidController"},
	    {"d46346ca-82a1-4cde-9546-77c86f893888", "?Oprom"},
	    {"e095affe-d4cd-4289-9b48-28f64e3d781d", "IntelRstOprom"},
	    {"fe612b72-203c-47b1-8560-a66d946eb371", "setupdata"},
	    {NULL, NULL},
	};
	for (guint i = 0; freeform_guids[i].guid != NULL; i++) {
		if (g_strcmp0(guid, freeform_guids[i].guid) == 0)
			return freeform_guids[i].str;
	}
	return NULL;
}

static gboolean
fu_efi_section_parse_freeform_subtype_guid(FuEfiSection *self,
					   GInputStream *stream,
					   FwupdInstallFlags flags,
					   GError **error)
{
	const gchar *guid_ui;
	g_autofree gchar *guid_str = NULL;
	g_autoptr(GByteArray) st = NULL;

	st = fu_struct_efi_section_freeform_subtype_guid_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* no idea */
	guid_str = fwupd_guid_to_string(fu_struct_efi_section_freeform_subtype_guid_get_guid(st),
					FWUPD_GUID_FLAG_MIXED_ENDIAN);
	guid_ui = fu_efi_section_freeform_subtype_guid_to_string(guid_str);
	if (guid_ui != NULL) {
		g_debug("ignoring FREEFORM_SUBTYPE_GUID %s [%s]", guid_str, guid_ui);
		return TRUE;
	}
	g_debug("unknown FREEFORM_SUBTYPE_GUID %s", guid_str);
	return TRUE;
}

static gboolean
fu_efi_section_parse(FuFirmware *firmware,
		     GInputStream *stream,
		     FwupdInstallFlags flags,
		     GError **error)
{
	FuEfiSection *self = FU_EFI_SECTION(firmware);
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);
	gsize offset = 0;
	gsize streamsz = 0;
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

	/* sanity check */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (size > streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid section size, got 0x%x from stream of size 0x%x",
			    (guint)size,
			    (guint)streamsz);
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
	partial_stream = fu_partial_input_stream_new(stream, offset, size - offset, error);
	if (partial_stream == NULL)
		return FALSE;
	fu_firmware_set_offset(firmware, offset);
	fu_firmware_set_size(firmware, size);
	if (!fu_firmware_set_stream(firmware, partial_stream, error))
		return FALSE;

	/* nested volume */
	if (priv->type == FU_EFI_SECTION_TYPE_VOLUME_IMAGE) {
		if (!fu_efi_section_parse_volume_image(self, partial_stream, flags, error)) {
			g_prefix_error(error, "failed to parse nested volume: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_GUID_DEFINED &&
		   g_strcmp0(fu_firmware_get_id(firmware), FU_EFI_SECTION_GUID_LZMA_COMPRESS) ==
		       0) {
		if (!fu_efi_section_parse_lzma_sections(self, partial_stream, flags, error)) {
			g_prefix_error(error, "failed to parse lzma section: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_GUID_DEFINED &&
		   g_strcmp0(fu_firmware_get_id(firmware),
			     "ced4eac6-49f3-4c12-a597-fc8c33447691") == 0) {
		g_debug("ignoring %s [0x%x] EFI section as self test",
			fu_efi_section_type_to_string(priv->type),
			priv->type);
	} else if (priv->type == FU_EFI_SECTION_TYPE_GUID_DEFINED) {
		g_warning("no idea how to decompress encapsulation section of type %s",
			  fu_firmware_get_id(firmware));
	} else if (priv->type == FU_EFI_SECTION_TYPE_USER_INTERFACE) {
		if (!fu_efi_section_parse_user_interface(self, partial_stream, flags, error)) {
			g_prefix_error(error, "failed to parse user interface: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_VERSION) {
		if (!fu_efi_section_parse_version(self, partial_stream, flags, error)) {
			g_prefix_error(error, "failed to parse version: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_COMPRESSION) {
		if (!fu_efi_section_parse_compression_sections(self,
							       partial_stream,
							       flags,
							       error)) {
			g_prefix_error(error, "failed to parse compression: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_FREEFORM_SUBTYPE_GUID) {
		if (!fu_efi_section_parse_freeform_subtype_guid(self,
								partial_stream,
								flags,
								error)) {
			g_prefix_error(error, "failed to parse compression: ");
			return FALSE;
		}
	} else if (priv->type == FU_EFI_SECTION_TYPE_PEI_DEPEX ||
		   priv->type == FU_EFI_SECTION_TYPE_DXE_DEPEX ||
		   priv->type == FU_EFI_SECTION_TYPE_MM_DEPEX ||
		   priv->type == FU_EFI_SECTION_TYPE_PE32 || priv->type == FU_EFI_SECTION_TYPE_TE ||
		   priv->type == FU_EFI_SECTION_TYPE_RAW) {
		g_debug("ignoring %s [0x%x] EFI section",
			fu_efi_section_type_to_string(priv->type),
			priv->type);
	} else {
		g_warning("no idea how to parse %s [0x%x] EFI section",
			  fu_efi_section_type_to_string(priv->type),
			  priv->type);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_section_write(FuFirmware *firmware, GError **error)
{
	FuEfiSection *self = FU_EFI_SECTION(firmware);
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);
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
fu_efi_section_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiSection *self = FU_EFI_SECTION(firmware);
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);
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
fu_efi_section_init(FuEfiSection *self)
{
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);
	priv->type = FU_EFI_SECTION_TYPE_RAW;
	fu_firmware_set_images_max(FU_FIRMWARE(self),
				   g_getenv("FWUPD_FUZZER_RUNNING") != NULL ? 10 : 2000);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	//	fu_firmware_set_alignment (FU_FIRMWARE (self), FU_FIRMWARE_ALIGNMENT_8);
	g_type_ensure(FU_TYPE_EFI_VOLUME);
}

static void
fu_efi_section_finalize(GObject *object)
{
	FuEfiSection *self = FU_EFI_SECTION(object);
	FuEfiSectionPrivate *priv = GET_PRIVATE(self);
	g_free(priv->user_interface);
	G_OBJECT_CLASS(fu_efi_section_parent_class)->finalize(object);
}

static void
fu_efi_section_class_init(FuEfiSectionClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_efi_section_finalize;
	firmware_class->parse = fu_efi_section_parse;
	firmware_class->write = fu_efi_section_write;
	firmware_class->build = fu_efi_section_build;
	firmware_class->export = fu_efi_section_export;
}

/**
 * fu_efi_section_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 2.0.0
 **/
FuFirmware *
fu_efi_section_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_SECTION, NULL));
}
