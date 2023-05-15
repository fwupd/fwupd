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
#include "fu-efi-struct.h"
#include "fu-mem.h"

/**
 * FuEfiFirmwareSection:
 *
 * A UEFI firmware section.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 type;
} FuEfiFirmwareSectionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiFirmwareSection, fu_efi_firmware_section, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_firmware_section_get_instance_private(o))

#define FU_EFI_FIRMWARE_SECTION_TYPE_COMPRESSION	   0x01
#define FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED	   0x02
#define FU_EFI_FIRMWARE_SECTION_TYPE_DISPOSABLE		   0x03
#define FU_EFI_FIRMWARE_SECTION_TYPE_PE32		   0x10
#define FU_EFI_FIRMWARE_SECTION_TYPE_PIC		   0x11
#define FU_EFI_FIRMWARE_SECTION_TYPE_TE			   0x12
#define FU_EFI_FIRMWARE_SECTION_TYPE_DXE_DEPEX		   0x13
#define FU_EFI_FIRMWARE_SECTION_TYPE_VERSION		   0x14
#define FU_EFI_FIRMWARE_SECTION_TYPE_USER_INTERFACE	   0x15
#define FU_EFI_FIRMWARE_SECTION_TYPE_COMPATIBILITY16	   0x16
#define FU_EFI_FIRMWARE_SECTION_TYPE_VOLUME_IMAGE	   0x17
#define FU_EFI_FIRMWARE_SECTION_TYPE_FREEFORM_SUBTYPE_GUID 0x18
#define FU_EFI_FIRMWARE_SECTION_TYPE_RAW		   0x19
#define FU_EFI_FIRMWARE_SECTION_TYPE_PEI_DEPEX		   0x1B
#define FU_EFI_FIRMWARE_SECTION_TYPE_MM_DEPEX		   0x1C

static const gchar *
fu_efi_firmware_section_type_to_string(guint8 type)
{
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_COMPRESSION)
		return "compression";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED)
		return "guid-defined";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_DISPOSABLE)
		return "disposable";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_PE32)
		return "pe32";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_PIC)
		return "pic";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_TE)
		return "te";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_DXE_DEPEX)
		return "dxe-depex";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_VERSION)
		return "version";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_USER_INTERFACE)
		return "user-interface";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_COMPATIBILITY16)
		return "compatibility16";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_VOLUME_IMAGE)
		return "volume-image";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_FREEFORM_SUBTYPE_GUID)
		return "freeform-subtype-guid";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_RAW)
		return "raw";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_PEI_DEPEX)
		return "pei-depex";
	if (type == FU_EFI_FIRMWARE_SECTION_TYPE_MM_DEPEX)
		return "mm-depex";
	return NULL;
}

static void
fu_efi_firmware_section_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);

	fu_xmlb_builder_insert_kx(bn, "type", priv->type);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv(bn,
					  "name",
					  fu_efi_guid_to_name(fu_firmware_get_id(firmware)));
		fu_xmlb_builder_insert_kv(bn,
					  "type_name",
					  fu_efi_firmware_section_type_to_string(priv->type));
	}
}

static gboolean
fu_efi_firmware_section_parse(FuFirmware *firmware,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION(firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	gsize bufsz = 0;
	guint32 size;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_efi_section_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;
	size = fu_struct_efi_section_get_size(st);
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
	if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED) {
		g_autofree gchar *guid_str = NULL;
		g_autoptr(GByteArray) st_def = NULL;
		st_def = fu_struct_efi_section_guid_defined_parse(buf, bufsz, st->len, error);
		if (st_def == NULL)
			return FALSE;
		guid_str = fwupd_guid_to_string(fu_struct_efi_section_guid_defined_get_name(st_def),
						FWUPD_GUID_FLAG_MIXED_ENDIAN);
		fu_firmware_set_id(firmware, guid_str);
		if (fu_struct_efi_section_guid_defined_get_offset(st_def) <
		    FU_STRUCT_EFI_SECTION_SIZE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid section size, got 0x%x",
				    (guint)fu_struct_efi_section_guid_defined_get_offset(st_def));
			return FALSE;
		}
	}

	/* create blob */
	offset += st->len;
	blob = fu_bytes_new_offset(fw, offset, size - offset, error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_offset(firmware, offset);
	fu_firmware_set_size(firmware, size);
	fu_firmware_set_bytes(firmware, blob);

	/* nested volume */
	if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_VOLUME_IMAGE) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_volume_new();
		if (!fu_firmware_parse(img, blob, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error))
			return FALSE;
		fu_firmware_add_image(firmware, img);

		/* LZMA */
	} else if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED &&
		   g_strcmp0(fu_firmware_get_id(firmware), FU_EFI_FIRMWARE_SECTION_LZMA_COMPRESS) ==
		       0) {
		g_autoptr(GBytes) blob_uncomp = NULL;

		/* parse all sections */
		blob_uncomp = fu_efi_firmware_decompress_lzma(blob, error);
		if (blob_uncomp == NULL)
			return FALSE;
		if (!fu_efi_firmware_parse_sections(firmware, blob_uncomp, flags, error))
			return FALSE;
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
	if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED) {
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
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "type", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->type = tmp;

	/* success */
	return TRUE;
}

static void
fu_efi_firmware_section_init(FuEfiFirmwareSection *self)
{
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE(self);
	priv->type = FU_EFI_FIRMWARE_SECTION_TYPE_RAW;
	//	fu_firmware_set_alignment (FU_FIRMWARE (self), FU_FIRMWARE_ALIGNMENT_8);
}

static void
fu_efi_firmware_section_class_init(FuEfiFirmwareSectionClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
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
