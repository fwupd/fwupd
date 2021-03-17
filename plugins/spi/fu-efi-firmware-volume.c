/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-firmware-filesystem.h"
#include "fu-efi-firmware-volume.h"

/**
 * SECTION:fu-uefi-firmware-volume
 * @short_description: UEFI Volume
 *
 * An object that represents a UEFI file volume.
 *
 * See also: #FuFirmware
 */

typedef struct {
	guint16			 attrs;
} FuEfiFirmwareVolumePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuEfiFirmwareVolume, fu_efi_firmware_volume, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_firmware_volume_get_instance_private (o))

#define FU_EFI_FIRMWARE_VOLUME_SIGNATURE			0x4856465F
#define FU_EFI_FIRMWARE_VOLUME_REVISION				0x02

#define FU_EFI_FIRMWARE_VOLUME_OFFSET_ZERO_VECTOR		0x00
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_GUID			0x10
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_LENGTH			0x20
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_SIGNATURE			0x28
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_ATTRS			0x2C
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_HDR_LEN			0x30
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_CHECKSUM			0x32
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_EXT_HDR			0x34
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_RESERVED			0x36
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_REVISION			0x37
#define FU_EFI_FIRMWARE_VOLUME_OFFSET_BLOCK_MAP			0x38
#define FU_EFI_FIRMWARE_VOLUME_SIZE				0x40

static void
fu_ifd_firmware_export (FuFirmware *firmware,
			FuFirmwareExportFlags flags,
			XbBuilderNode *bn)
{
	FuEfiFirmwareVolume *self = FU_EFI_FIRMWARE_VOLUME (firmware);
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE (self);
	fu_xmlb_builder_insert_kx (bn, "attrs", priv->attrs);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv (bn, "name",
					   fu_efi_guid_to_name (fu_firmware_get_id (firmware)));
	}
}

static gboolean
fu_efi_firmware_volume_parse (FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuEfiFirmwareVolume *self = FU_EFI_FIRMWARE_VOLUME (firmware);
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE (self);
	fwupd_guid_t guid = { 0x0 };
	gsize blockmap_sz = 0;
	gsize bufsz = 0;
	gsize offset = 0;
	guint16 checksum = 0;
	guint16 ext_hdr = 0;
	guint16 hdr_length = 0;
	guint32 attrs = 0;
	guint32 sig = 0;
	guint64 fv_length = 0;
	guint8 revision = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autofree gchar *guid_str = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* sanity check */
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_SIGNATURE,
					 &sig, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read signature: ");
		return FALSE;
	}
	if (sig != FU_EFI_FIRMWARE_VOLUME_SIGNATURE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "EFI FV signature invalid, got 0x%x, expected 0x%x",
			     sig, (guint) FU_EFI_FIRMWARE_VOLUME_SIGNATURE);
		return FALSE;
	}

	/* guid */
	if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,		/* dst */
			     buf, bufsz, offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_GUID,	/* src */
			     sizeof(guid), error)) {
		g_prefix_error (error, "failed to read GUID: ");
		return FALSE;
	}
	guid_str = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	g_debug ("volume GUID: %s [%s]", guid_str, fu_efi_guid_to_name (guid_str));

	/* length */
	if (!fu_common_read_uint64_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_LENGTH,
					 &fv_length, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read length: ");
		return FALSE;
	}
	if (fv_length == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid volume length");
		return FALSE;
	}
	fu_firmware_set_size (firmware, fv_length);
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_ATTRS,
					 &attrs, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read attrs: ");
		return FALSE;
	}
	fu_firmware_set_alignment (firmware, (attrs & 0x00ff0000) >> 16);
	priv->attrs = attrs & 0xffff;
	if (!fu_common_read_uint16_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_HDR_LEN,
					 &hdr_length, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read hdr_length: ");
		return FALSE;
	}
	if (hdr_length < FU_EFI_FIRMWARE_VOLUME_SIZE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid volume header length");
		return FALSE;
	}
	if (!fu_common_read_uint16_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_CHECKSUM,
					 &checksum, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read checksum: ");
		return FALSE;
	}
	if (!fu_common_read_uint16_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_EXT_HDR,
					 &ext_hdr, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read ext_hdr: ");
		return FALSE;
	}
	if (!fu_common_read_uint8_safe (buf, bufsz,
					offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_REVISION,
					&revision, error))
		return FALSE;
	if (revision != FU_EFI_FIRMWARE_VOLUME_REVISION) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "revision invalid, got 0x%x, expected 0x%x",
			     revision, (guint) FU_EFI_FIRMWARE_VOLUME_REVISION);
		return FALSE;
	}

	/* verify checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint16 checksum_verify = 0;
		for (guint j = 0; j < hdr_length; j += sizeof(guint16)) {
			guint16 checksum_tmp = 0;
			if (!fu_common_read_uint16_safe (buf, bufsz, offset + j,
							 &checksum_tmp,
							 G_LITTLE_ENDIAN, error)) {
				g_prefix_error (error, "failed to hdr checksum 0x%x: ", j);
				return FALSE;
			}
			checksum_verify += checksum_tmp;
		}
		if (checksum_verify != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "checksum invalid, got %02x, expected %02x",
				     checksum_verify, checksum);
			return FALSE;
		}
	}

	/* add image */
	blob = fu_common_bytes_new_offset (fw, offset + hdr_length, fv_length - hdr_length, error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_offset (firmware, offset);
	fu_firmware_set_id (firmware, guid_str);
	fu_firmware_set_size (firmware, fv_length);

	/* parse, which might cascade and do something like FFS2 */
	if (g_strcmp0 (guid_str, FU_EFI_FIRMWARE_VOLUME_GUID_FFS2) == 0) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_filesystem_new ();
		fu_firmware_set_alignment (img, fu_firmware_get_alignment (firmware));
		if (!fu_firmware_parse (img, blob, flags, error))
			return FALSE;
		fu_firmware_add_image (firmware, img);
	} else {
		fu_firmware_set_bytes (firmware, blob);
	}

	/* skip the blockmap */
	offset += FU_EFI_FIRMWARE_VOLUME_OFFSET_BLOCK_MAP;
	while (offset < bufsz) {
		guint32 num_blocks = 0;
		guint32 length = 0;
		if (!fu_common_read_uint32_safe (buf, bufsz, offset,
						 &num_blocks, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (!fu_common_read_uint32_safe (buf, bufsz, offset + sizeof(guint32),
						 &length, G_LITTLE_ENDIAN, error))
			return FALSE;
		offset += 2 * sizeof(guint32);
		if (num_blocks == 0x0 && length == 0x0)
			break;
		blockmap_sz += (gsize) num_blocks * (gsize) length;
	}
	if (blockmap_sz < (gsize) fv_length) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "blocks allocated is less than volume length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_firmware_volume_write (FuFirmware *firmware, GError **error)
{
	FuEfiFirmwareVolume *self = FU_EFI_FIRMWARE_VOLUME (firmware);
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	fwupd_guid_t guid = { 0x0 };
	guint16 checksum = 0;
	guint32 hdr_length = 0x48;
	guint64 fv_length;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(FuFirmware) img = NULL;

	/* zero vector */
	for (guint i = 0; i < 0x10; i++)
		fu_byte_array_append_uint8 (buf, 0x0);

	/* GUID */
	if (fu_firmware_get_id (firmware) == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "no GUID set for EFI FV");
		return NULL;
	}
	if (!fwupd_guid_from_string (fu_firmware_get_id (firmware),
				     &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN,
				     error))
		return NULL;
	g_byte_array_append (buf, (const guint8 *) &guid, sizeof(guid));

	/* length */
	img = fu_firmware_get_image_by_id (firmware, NULL, NULL);
	if (img != NULL) {
		img_blob = fu_firmware_write (img, error);
		if (img_blob == NULL) {
			g_prefix_error (error, "no EFI FV child payload: ");
			return NULL;
		}
	} else {
		img_blob = fu_firmware_get_bytes (firmware, error);
		if (img_blob == NULL) {
			g_prefix_error (error, "no EFI FV payload: ");
			return NULL;
		}
	}
	fv_length = fu_common_align_up (hdr_length + g_bytes_get_size (img_blob),
					fu_firmware_get_alignment (firmware));
	fu_byte_array_append_uint64 (buf, fv_length, G_LITTLE_ENDIAN);

	/* signature */
	fu_byte_array_append_uint32 (buf, FU_EFI_FIRMWARE_VOLUME_SIGNATURE, G_LITTLE_ENDIAN);

	/* attributes */
	fu_byte_array_append_uint32 (buf,
				     priv->attrs |
				     ((guint32) fu_firmware_get_alignment (firmware) << 16),
				     G_LITTLE_ENDIAN);

	/* header length */
	fu_byte_array_append_uint16 (buf, hdr_length, G_LITTLE_ENDIAN);

	/* checksum (will fixup) */
	fu_byte_array_append_uint16 (buf, 0x0, G_LITTLE_ENDIAN);

	/* ext header offset */
	fu_byte_array_append_uint16 (buf, 0x0, G_LITTLE_ENDIAN);

	/* reserved */
	fu_byte_array_append_uint8 (buf, 0x0);

	/* revision */
	fu_byte_array_append_uint8 (buf, FU_EFI_FIRMWARE_VOLUME_REVISION);

	/* blockmap */
	fu_byte_array_append_uint32 (buf, fv_length, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (buf, 0x1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (buf, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (buf, 0x0, G_LITTLE_ENDIAN);

	/* fix up checksum */
	for (guint j = buf->len - hdr_length; j < buf->len; j += sizeof(guint16)) {
		guint16 checksum_tmp = 0;
		if (!fu_common_read_uint16_safe (buf->data, buf->len, j,
						 &checksum_tmp, G_LITTLE_ENDIAN, error))
			return NULL;
		checksum += checksum_tmp;
	}
	if (!fu_common_write_uint16_safe (buf->data, buf->len,
					  buf->len - 0x16,
					  0x10000 - checksum,
					  G_LITTLE_ENDIAN,
					  error))
		return NULL;

	/* pad contents to alignment */
	fu_byte_array_append_bytes (buf, img_blob);
	fu_byte_array_set_size_full (buf, fv_length, 0xFF);

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static void
fu_efi_firmware_volume_init (FuEfiFirmwareVolume *self)
{
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE (self);
	priv->attrs = 0xfeff;
}

static void
fu_efi_firmware_volume_class_init (FuEfiFirmwareVolumeClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_efi_firmware_volume_parse;
	klass_firmware->write = fu_efi_firmware_volume_write;
	klass_firmware->export = fu_ifd_firmware_export;
}

/**
 * fu_efi_firmware_volume_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.0
 **/
FuFirmware *
fu_efi_firmware_volume_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_EFI_FIRMWARE_VOLUME, NULL));
}
