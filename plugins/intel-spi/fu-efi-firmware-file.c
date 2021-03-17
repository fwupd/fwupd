/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-efi-common.h"
#include "fu-efi-firmware-file.h"
#include "fu-efi-firmware-section.h"
#include "fu-efi-firmware-common.h"

typedef struct {
	guint8			 type;
	guint8			 attrib;
} FuEfiFirmwareFilePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuEfiFirmwareFile, fu_efi_firmware_file, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_firmware_file_get_instance_private (o))

#define FU_EFI_FIRMWARE_FILE_ATTRIB_NONE			0x00
#define FU_EFI_FIRMWARE_FILE_ATTRIB_LARGE_FILE			0x01
#define FU_EFI_FIRMWARE_FILE_ATTRIB_DATA_ALIGNMENT_2		0x02
#define FU_EFI_FIRMWARE_FILE_ATTRIB_FIXED			0x04
#define FU_EFI_FIRMWARE_FILE_ATTRIB_DATA_ALIGNMENT		0x38
#define FU_EFI_FIRMWARE_FILE_ATTRIB_CHECKSUM			0x40

#define FU_EFI_FIRMWARE_FILE_TYPE_ALL				0x00
#define FU_EFI_FIRMWARE_FILE_TYPE_RAW				0x01
#define FU_EFI_FIRMWARE_FILE_TYPE_FREEFORM			0x02
#define FU_EFI_FIRMWARE_FILE_TYPE_SECURITY_CORE			0x03
#define FU_EFI_FIRMWARE_FILE_TYPE_PEI_CORE			0x04
#define FU_EFI_FIRMWARE_FILE_TYPE_DXE_CORE			0x05
#define FU_EFI_FIRMWARE_FILE_TYPE_PEIM				0x06
#define FU_EFI_FIRMWARE_FILE_TYPE_DRIVER			0x07
#define FU_EFI_FIRMWARE_FILE_TYPE_COMBINED_PEIM_DRIVER		0x08
#define FU_EFI_FIRMWARE_FILE_TYPE_APPLICATION			0x09
#define FU_EFI_FIRMWARE_FILE_TYPE_MM				0x0A
#define FU_EFI_FIRMWARE_FILE_TYPE_FIRMWARE_VOLUME_IMAGE		0x0B
#define FU_EFI_FIRMWARE_FILE_TYPE_COMBINED_MM_DXE		0x0C
#define FU_EFI_FIRMWARE_FILE_TYPE_MM_CORE			0x0D
#define FU_EFI_FIRMWARE_FILE_TYPE_MM_STANDALONE			0x0E
#define FU_EFI_FIRMWARE_FILE_TYPE_MM_CORE_STANDALONE		0x0F
#define FU_EFI_FIRMWARE_FILE_TYPE_FFS_PAD			0xF0

#define FU_EFI_FIRMWARE_FILE_OFFSET_NAME			0x00
#define FU_EFI_FIRMWARE_FILE_OFFSET_HDR_CHECKSUM		0x10
#define FU_EFI_FIRMWARE_FILE_OFFSET_DATA_CHECKSUM		0x11
#define FU_EFI_FIRMWARE_FILE_OFFSET_TYPE			0x12
#define FU_EFI_FIRMWARE_FILE_OFFSET_ATTRS			0x13
#define FU_EFI_FIRMWARE_FILE_OFFSET_SIZE			0x14
#define FU_EFI_FIRMWARE_FILE_OFFSET_STATE			0x17
#define FU_EFI_FIRMWARE_FILE_SIZE				0x18

static const gchar *
fu_efi_firmware_file_type_to_string (guint8 type)
{
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_ALL)
		return "all";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_RAW)
		return "raw";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_FREEFORM)
		return "freeform";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_SECURITY_CORE)
		return "security-core";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_PEI_CORE)
		return "pei-core";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_DXE_CORE)
		return "dxe-core";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_PEIM)
		return "peim";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_DRIVER)
		return "driver";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_COMBINED_PEIM_DRIVER)
		return "combined-peim-driver";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_APPLICATION)
		return "application";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_MM)
		return "mm";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_FIRMWARE_VOLUME_IMAGE)
		return "firmware-volume-image";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_COMBINED_MM_DXE)
		return "combined-mm-dxe";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_MM_CORE)
		return "mm-core";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_MM_STANDALONE)
		return "mm-standalone";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_MM_CORE_STANDALONE)
		return "core-standalone";
	if (type == FU_EFI_FIRMWARE_FILE_TYPE_FFS_PAD)
		return "ffs-pad";
	return NULL;
}

static void
fu_efi_firmware_file_export (FuFirmware *firmware,
			     FuFirmwareExportFlags flags,
			     XbBuilderNode *bn)
{
	FuEfiFirmwareFile *self = FU_EFI_FIRMWARE_FILE (firmware);
	FuEfiFirmwareFilePrivate *priv = GET_PRIVATE (self);

	fu_xmlb_builder_insert_kx (bn, "attrib", priv->attrib);
	fu_xmlb_builder_insert_kx (bn, "type", priv->type);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv (bn, "name",
					   fu_efi_guid_to_name (fu_firmware_get_id (firmware)));
		fu_xmlb_builder_insert_kv (bn, "type_name",
					   fu_efi_firmware_file_type_to_string (priv->type));
	}
}

static guint8
fu_efi_firmware_file_data_checksum8 (GBytes *blob)
{
	gsize bufsz = 0;
	guint8 checksum = 0;
	const guint8 *buf = g_bytes_get_data (blob, &bufsz);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return 0x100 - checksum;
}

static guint8
fu_efi_firmware_file_hdr_checksum8 (GBytes *blob)
{
	gsize bufsz = 0;
	guint8 checksum = 0;
	const guint8 *buf = g_bytes_get_data (blob, &bufsz);
	for (gsize i = 0; i < bufsz; i++) {
		if (i == FU_EFI_FIRMWARE_FILE_OFFSET_HDR_CHECKSUM)
			continue;
		if (i == FU_EFI_FIRMWARE_FILE_OFFSET_DATA_CHECKSUM)
			continue;
		if (i == FU_EFI_FIRMWARE_FILE_OFFSET_STATE)
			continue;
		checksum += buf[i];
	}
	return 0x100 - checksum;
}

static gboolean
fu_efi_firmware_file_parse (FuFirmware *firmware,
			    GBytes *fw,
			    guint64 addr_start,
			    guint64 addr_end,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuEfiFirmwareFile *self = FU_EFI_FIRMWARE_FILE (firmware);
	FuEfiFirmwareFilePrivate *priv = GET_PRIVATE (self);
	gsize bufsz = 0;
	guint32 size = 0x0;
	guint8 data_checksum = 0x0;
	guint8 hdr_checksum = 0x0;
	guint8 img_state = 0x0;
	fwupd_guid_t guid = { 0x0 };
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autofree gchar *guid_str = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* header */
	if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,		/* dst */
			     buf, bufsz, FU_EFI_FIRMWARE_FILE_OFFSET_NAME,	/* src */
			     sizeof(guid), error))
		return FALSE;
	guid_str = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	fu_firmware_set_id (firmware, guid_str);
	if (!fu_common_read_uint8_safe (buf, bufsz,
					FU_EFI_FIRMWARE_FILE_OFFSET_STATE,
					&img_state, error))
		return FALSE;
	if (img_state != 0xF8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "state invalid, got 0x%x, expected 0x%x",
			     img_state, (guint) 0xF8);
		return FALSE;
	}
	if (!fu_common_read_uint8_safe (buf, bufsz,
					FU_EFI_FIRMWARE_FILE_OFFSET_HDR_CHECKSUM,
					&hdr_checksum, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, bufsz,
					FU_EFI_FIRMWARE_FILE_OFFSET_DATA_CHECKSUM,
					&data_checksum, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, bufsz,
					FU_EFI_FIRMWARE_FILE_OFFSET_TYPE,
					&priv->type, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, bufsz,
					FU_EFI_FIRMWARE_FILE_OFFSET_ATTRS,
					&priv->attrib, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz, /* uint24_t! */
					 FU_EFI_FIRMWARE_FILE_OFFSET_SIZE,
					 &size, G_LITTLE_ENDIAN, error))
		return FALSE;
	size &= 0xFFFFFF;
	if (size < FU_EFI_FIRMWARE_FILE_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid FFS length, got 0x%x",
			     (guint) size);
		return FALSE;
	}

	/* verify header checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		g_autoptr(GBytes) hdr_blob = g_bytes_new_from_bytes (fw, 0x0, FU_EFI_FIRMWARE_FILE_SIZE);
		guint8 hdr_checksum_verify = fu_efi_firmware_file_hdr_checksum8 (hdr_blob);
		if (hdr_checksum_verify != hdr_checksum) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "checksum invalid, got %02x, expected %02x",
				     hdr_checksum_verify, hdr_checksum);
			return FALSE;
		}
	}

	/* add simple blob */
	blob = fu_common_bytes_new_offset (fw,
					   FU_EFI_FIRMWARE_FILE_SIZE,
					   size - FU_EFI_FIRMWARE_FILE_SIZE,
					   error);
	if (blob == NULL)
		return FALSE;

	/* add fv-image */
	if (priv->type == FU_EFI_FIRMWARE_FILE_TYPE_FIRMWARE_VOLUME_IMAGE) {
		if (!fu_efi_firmware_parse_sections (firmware, blob, flags, error))
			return FALSE;
	} else {
		fu_firmware_set_bytes (firmware, blob);
	}

	/* verify data checksum */
	if ((priv->attrib & FU_EFI_FIRMWARE_FILE_ATTRIB_CHECKSUM) > 0 &&
	    (flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 data_checksum_verify = fu_efi_firmware_file_data_checksum8 (blob);
		if (data_checksum_verify != data_checksum) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "checksum invalid, got %02x, expected %02x",
				     data_checksum_verify, data_checksum);
			return FALSE;
		}
	}

	/* align size for volume */
	fu_firmware_set_size (firmware,
			      fu_common_align_up (size,
						  fu_firmware_get_alignment (firmware)));

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_firmware_file_write_sections (FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	/* no sections defined */
	if (images->len == 0)
		return fu_firmware_get_bytes (firmware, error);

	/* add each section */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		g_autoptr(GBytes) blob = NULL;
		fu_firmware_set_offset (img, buf->len);
		blob = fu_firmware_write (img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes (buf, blob);
		fu_byte_array_align_up (buf, fu_firmware_get_alignment (img), 0xFF);
	}

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static GBytes *
fu_efi_firmware_file_write (FuFirmware *firmware, GError **error)
{
	FuEfiFirmwareFile *self = FU_EFI_FIRMWARE_FILE (firmware);
	FuEfiFirmwareFilePrivate *priv = GET_PRIVATE (self);
	fwupd_guid_t guid = { 0x0 };
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) hdr_blob = NULL;

	/* simple blob for now */
	blob = fu_efi_firmware_file_write_sections (firmware, error);
	if (blob == NULL)
		return NULL;

	/* header */
	if (!fwupd_guid_from_string (fu_firmware_get_id (firmware), &guid,
				     FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return NULL;
	g_byte_array_append (buf, (guint8 *) &guid, sizeof(guid));
	fu_byte_array_append_uint8 (buf, 0x0); /* hdr_checksum */
	fu_byte_array_append_uint8 (buf, fu_efi_firmware_file_data_checksum8 (blob));
	fu_byte_array_append_uint8 (buf, priv->type); /* data_checksum */
	fu_byte_array_append_uint8 (buf, priv->attrib); /* data_checksum */
	fu_byte_array_append_uint32 (buf, g_bytes_get_size (blob) + FU_EFI_FIRMWARE_FILE_SIZE, G_LITTLE_ENDIAN);
	buf->data[FU_EFI_FIRMWARE_FILE_OFFSET_STATE] = 0xF8; /* overwrite the LSB of size */

	/* fix up header checksum */
	hdr_blob = g_bytes_new (buf->data, buf->len);
	buf->data[FU_EFI_FIRMWARE_FILE_OFFSET_HDR_CHECKSUM] = fu_efi_firmware_file_hdr_checksum8 (hdr_blob);

	/* payload */
	fu_byte_array_append_bytes (buf, blob);

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static gboolean
fu_efi_firmware_file_build (FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiFirmwareFile *self = FU_EFI_FIRMWARE_FILE (firmware);
	FuEfiFirmwareFilePrivate *priv = GET_PRIVATE (self);
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint (n, "type", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->type = tmp;
	tmp = xb_node_query_text_as_uint (n, "attrib", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->attrib = tmp;

	/* success */
	return TRUE;
}

static void
fu_efi_firmware_file_init (FuEfiFirmwareFile *self)
{
	FuEfiFirmwareFilePrivate *priv = GET_PRIVATE (self);
	priv->attrib = FU_EFI_FIRMWARE_FILE_ATTRIB_NONE;
	priv->type = FU_EFI_FIRMWARE_FILE_TYPE_RAW;
	fu_firmware_set_alignment (FU_FIRMWARE (self), FU_FIRMWARE_ALIGNMENT_8);
}

static void
fu_efi_firmware_file_class_init (FuEfiFirmwareFileClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_efi_firmware_file_parse;
	klass_firmware->write = fu_efi_firmware_file_write;
	klass_firmware->build = fu_efi_firmware_file_build;
	klass_firmware->export = fu_efi_firmware_file_export;
}

/**
 * fu_efi_firmware_file_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.0
 **/
FuFirmware *
fu_efi_firmware_file_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_EFI_FIRMWARE_FILE, NULL));
}
