/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-efi-common.h"
#include "fu-efi-firmware-section.h"
#include "fu-efi-firmware-common.h"
#include "fu-efi-firmware-volume.h"

typedef struct {
	guint8			 type;
} FuEfiFirmwareSectionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuEfiFirmwareSection, fu_efi_firmware_section, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_firmware_section_get_instance_private (o))

#define FU_EFI_FIRMWARE_SECTION_OFFSET_SIZE			0x00
#define FU_EFI_FIRMWARE_SECTION_OFFSET_TYPE			0x03
#define FU_EFI_FIRMWARE_SECTION_SIZE				0x04

/* only GUID defined */
#define FU_EFI_FIRMWARE_SECTION_OFFSET_GUID_NAME		0x04
#define FU_EFI_FIRMWARE_SECTION_OFFSET_GUID_DATA_OFFSET		0x14
#define FU_EFI_FIRMWARE_SECTION_OFFSET_GUID_ATTR		0x16

#define FU_EFI_FIRMWARE_SECTION_TYPE_COMPRESSION		0x01
#define FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED		0x02
#define FU_EFI_FIRMWARE_SECTION_TYPE_DISPOSABLE			0x03
#define FU_EFI_FIRMWARE_SECTION_TYPE_PE32			0x10
#define FU_EFI_FIRMWARE_SECTION_TYPE_PIC			0x11
#define FU_EFI_FIRMWARE_SECTION_TYPE_TE				0x12
#define FU_EFI_FIRMWARE_SECTION_TYPE_DXE_DEPEX			0x13
#define FU_EFI_FIRMWARE_SECTION_TYPE_VERSION			0x14
#define FU_EFI_FIRMWARE_SECTION_TYPE_USER_INTERFACE		0x15
#define FU_EFI_FIRMWARE_SECTION_TYPE_COMPATIBILITY16		0x16
#define FU_EFI_FIRMWARE_SECTION_TYPE_VOLUME_IMAGE		0x17
#define FU_EFI_FIRMWARE_SECTION_TYPE_FREEFORM_SUBTYPE_GUID	0x19
#define FU_EFI_FIRMWARE_SECTION_TYPE_RAW			0x10
#define FU_EFI_FIRMWARE_SECTION_TYPE_PEI_DEPEX			0x1B
#define FU_EFI_FIRMWARE_SECTION_TYPE_MM_DEPEX			0x1C

static const gchar *
fu_efi_firmware_section_type_to_string (guint8 type)
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
fu_efi_firmware_section_export (FuFirmware *firmware,
				FuFirmwareExportFlags flags,
				XbBuilderNode *bn)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION (firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE (self);

	fu_xmlb_builder_insert_kx (bn, "type", priv->type);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv (bn, "name",
					   fu_efi_guid_to_name (fu_firmware_get_id (firmware)));
		fu_xmlb_builder_insert_kv (bn, "type_name",
					   fu_efi_firmware_section_type_to_string (priv->type));
	}
}

static gboolean
fu_efi_firmware_section_parse (FuFirmware *firmware,
			       GBytes *fw,
			       guint64 addr_start,
			       guint64 addr_end,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION (firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE (self);
	gsize bufsz = 0;
	guint16 attr = 0x0;
	guint16 offset = FU_EFI_FIRMWARE_SECTION_SIZE;
	guint32 size = 0x0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(GBytes) blob = NULL;

	if (!fu_common_read_uint32_safe (buf, bufsz, /* uint24_t! */
					 FU_EFI_FIRMWARE_SECTION_OFFSET_SIZE,
					 &size, G_LITTLE_ENDIAN, error))
		return FALSE;
	size &= 0xFFFFFF;
	if (size < FU_EFI_FIRMWARE_SECTION_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid section size, got 0x%x",
			     (guint) size);
		return FALSE;
	}
	if (!fu_common_read_uint8_safe (buf, bufsz,
					FU_EFI_FIRMWARE_SECTION_OFFSET_TYPE,
					&priv->type, error))
		return FALSE;

	/* name */
	if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED) {
		fwupd_guid_t guid = { 0x0 };
		g_autofree gchar *guid_str = NULL;
		if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,		/* dst */
				     buf, bufsz, FU_EFI_FIRMWARE_SECTION_OFFSET_GUID_NAME,	/* src */
				     sizeof(guid), error))
			return FALSE;
		guid_str = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
		fu_firmware_set_id (firmware, guid_str);
		if (!fu_common_read_uint16_safe (buf, bufsz,
						 FU_EFI_FIRMWARE_SECTION_OFFSET_GUID_DATA_OFFSET,
						 &offset, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (offset < FU_EFI_FIRMWARE_SECTION_SIZE) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid section size, got 0x%x",
				     (guint) size);
			return FALSE;
		}
		if (!fu_common_read_uint16_safe (buf, bufsz,
						 FU_EFI_FIRMWARE_SECTION_OFFSET_GUID_ATTR,
						 &attr, G_LITTLE_ENDIAN, error))
			return FALSE;
	}

	/* create blob */
	blob = fu_common_bytes_new_offset (fw, offset, size - offset, error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_offset (firmware, offset);
	fu_firmware_set_size (firmware, size);
	fu_firmware_set_bytes (firmware, blob);

	/* nested volume */
	if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_VOLUME_IMAGE) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_volume_new ();
		if (!fu_firmware_parse (img, blob, flags, error))
			return FALSE;
		fu_firmware_add_image (firmware, img);

	/* LZMA */
	} else if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED &&
		   g_strcmp0 (fu_firmware_get_id (firmware), FU_EFI_FIRMWARE_SECTION_LZMA_COMPRESS) == 0) {
		g_autoptr(GBytes) blob_uncomp = NULL;

		/* parse all sections */
		blob_uncomp = fu_efi_firmware_decompress_lzma (blob, error);
		if (blob_uncomp == NULL)
			return FALSE;
		if (!fu_efi_firmware_parse_sections (firmware, blob_uncomp, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_firmware_section_write (FuFirmware *firmware, GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION (firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GBytes) blob = NULL;

	/* simple blob for now */
	blob = fu_firmware_get_bytes (firmware, error);
	if (blob == NULL)
		return NULL;

	/* header */
	fu_byte_array_append_uint32 (buf, 0x0, G_LITTLE_ENDIAN); /* will fixup */
	if (priv->type == FU_EFI_FIRMWARE_SECTION_TYPE_GUID_DEFINED) {
		fwupd_guid_t guid = { 0x0 };
		if (!fwupd_guid_from_string (fu_firmware_get_id (firmware), &guid,
					     FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
			return NULL;
		g_byte_array_append (buf, (guint8 *) &guid, sizeof(guid));
		fu_byte_array_append_uint16 (buf, buf->len + 0x4, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16 (buf, 0x0, G_LITTLE_ENDIAN);
	}

	/* correct size and type in common header */
	if (!fu_common_write_uint32_safe (buf->data, buf->len, /* uint24_t! */
					  FU_EFI_FIRMWARE_SECTION_OFFSET_SIZE,
					  buf->len + g_bytes_get_size (blob),
					  G_LITTLE_ENDIAN, error))
		return NULL;
	buf->data[FU_EFI_FIRMWARE_SECTION_OFFSET_TYPE] = priv->type;

	/* blob */
	fu_byte_array_append_bytes (buf, blob);
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static gboolean
fu_efi_firmware_section_build (FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiFirmwareSection *self = FU_EFI_FIRMWARE_SECTION (firmware);
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE (self);
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint (n, "type", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->type = tmp;

	/* success */
	return TRUE;
}

static void
fu_efi_firmware_section_init (FuEfiFirmwareSection *self)
{
	FuEfiFirmwareSectionPrivate *priv = GET_PRIVATE (self);
	priv->type = FU_EFI_FIRMWARE_SECTION_TYPE_RAW;
//	fu_firmware_set_alignment (FU_FIRMWARE (self), 3);
}

static void
fu_efi_firmware_section_class_init (FuEfiFirmwareSectionClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
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
 * Since: 1.6.0
 **/
FuFirmware *
fu_efi_firmware_section_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_EFI_FIRMWARE_SECTION, NULL));
}
