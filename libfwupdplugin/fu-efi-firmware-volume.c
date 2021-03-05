/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-image.h"
#include "fu-efi-firmware-volume.h"

/**
 * SECTION:fu-uefi-firmware-volume
 * @short_description: UEFI Volume
 *
 * An object that represents a UEFI file volume image.
 *
 * See also: #FuFirmware
 */

G_DEFINE_TYPE (FuEfiFirmwareVolume, fu_efi_firmware_volume, FU_TYPE_FIRMWARE)

#define FU_EFI_FIT_SIGNATURE					0x5449465F
#define FU_EFI_FIT_SIZE						0x150000

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

static gboolean
fu_efi_firmware_volume_parse_one (FuEfiFirmwareVolume *self,
				  GBytes *fw,
				  gsize *offset_val,
				  guint32 img_cnt,
				  FwupdInstallFlags flags,
				  GError **error)
{
	fwupd_guid_t guid = { 0x0 };
	gsize blockmap_sz = 0;
	gsize bufsz = 0;
	gsize offset = *offset_val;
	guint16 checksum = 0;
	guint16 ext_hdr = 0;
	guint16 hdr_length = 0;
	guint32 attrs = 0;
	guint32 sig = 0;
	guint64 fv_length = 0;
	guint8 revision = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autofree gchar *guid_str = NULL;
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* check if this is _FIT */
	if (!fu_common_read_uint32_safe (buf, bufsz, offset,
					 &sig, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read start signature: ");
		return FALSE;
	}
	if (sig == FU_EFI_FIT_SIGNATURE) {
		*offset_val += FU_EFI_FIT_SIZE;
		return TRUE;
	}
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
			     "signature invalid, got 0x%x, expected 0x%x",
			     sig, (guint) FU_EFI_FIRMWARE_VOLUME_SIGNATURE);
		return FALSE;
	}

	/* guid */
	if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,		/* dst */
			     buf, bufsz, offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_GUID, 	/* src */
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
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 offset + FU_EFI_FIRMWARE_VOLUME_OFFSET_ATTRS,
					 &attrs, G_LITTLE_ENDIAN, error)) {
		g_prefix_error (error, "failed to read attrs: ");
		return FALSE;
	}
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
	img = fu_efi_image_new ();
	fu_firmware_image_set_bytes (img, blob);
	fu_firmware_image_set_addr (img, offset);
	fu_firmware_image_set_id (img, guid_str);
	fu_firmware_image_set_idx (img, img_cnt);
	fu_firmware_add_image (FU_FIRMWARE (self), img);

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
	*offset_val += fv_length;
	return TRUE;
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
	gsize bufsz = 0;
	gsize offset = 0;
	guint32 sig;
	guint32 img_cnt = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);

	/* jump 16MiB as required */
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 FU_EFI_FIRMWARE_VOLUME_OFFSET_SIGNATURE,
					 &sig, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (sig != FU_EFI_FIRMWARE_VOLUME_SIGNATURE)
		offset += 0x100000;

	/* read each volume in order */
	while (offset < bufsz) {
		if (!fu_efi_firmware_volume_parse_one (self, fw, &offset, img_cnt, flags, error))
			return FALSE;
		img_cnt++;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_firmware_volume_write_one (FuFirmwareImage *img, GByteArray *buf, GError **error)
{
	fwupd_guid_t guid = { 0x0 };
	guint16 checksum = 0;
	guint32 hdr_length = 0x48;
	guint64 fv_length = hdr_length;
	g_autoptr(GBytes) img_blob = NULL;

	/* zero vector */
	for (guint i = 0; i < 0x10; i++)
		fu_byte_array_append_uint8 (buf, 0x0);

	/* GUID */
	if (!fwupd_guid_from_string (fu_firmware_image_get_id (img),
				     &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN,
				     error))
		return FALSE;
	g_byte_array_append (buf, (const guint8 *) &guid, sizeof(guid));

	/* length */
	img_blob = fu_firmware_image_write (img, error);
	if (img_blob == NULL)
		return FALSE;
	fv_length += g_bytes_get_size (img_blob);
	fu_byte_array_append_uint64 (buf, fv_length, G_LITTLE_ENDIAN);

	/* signature */
	fu_byte_array_append_uint32 (buf, FU_EFI_FIRMWARE_VOLUME_SIGNATURE, G_LITTLE_ENDIAN);

	/* attributes */
	fu_byte_array_append_uint32 (buf, 0x4feff, G_LITTLE_ENDIAN);

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
			return FALSE;
		checksum += checksum_tmp;
	}
	if (!fu_common_write_uint16_safe (buf->data, buf->len,
					  buf->len - 0x16,
					  0x10000 - checksum,
					  G_LITTLE_ENDIAN,
					  error))
		return FALSE;

	/* contents */
	g_byte_array_append (buf,
			     (const guint8 *) g_bytes_get_data (img_blob, NULL),
			     g_bytes_get_size (img_blob));
	return TRUE;
}

static GBytes *
fu_efi_firmware_volume_write (FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);

	/* add each volume */
	for (guint i = 0; i < images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (images, i);
		if (!fu_efi_firmware_volume_write_one (img, buf, error))
			return NULL;
	}

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static void
fu_efi_firmware_volume_init (FuEfiFirmwareVolume *self)
{
}

static void
fu_efi_firmware_volume_class_init (FuEfiFirmwareVolumeClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_efi_firmware_volume_parse;
	klass_firmware->write = fu_efi_firmware_volume_write;
}

/**
 * fu_efi_firmware_volume_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.5.8
 **/
FuFirmware *
fu_efi_firmware_volume_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_EFI_FIRMWARE_VOLUME, NULL));
}
