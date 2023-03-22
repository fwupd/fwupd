/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-efi-common.h"
#include "fu-efi-firmware-filesystem.h"
#include "fu-efi-firmware-volume.h"
#include "fu-efi-struct.h"
#include "fu-sum.h"

/**
 * FuEfiFirmwareVolume:
 *
 * A UEFI file volume.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint16 attrs;
} FuEfiFirmwareVolumePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiFirmwareVolume, fu_efi_firmware_volume, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_firmware_volume_get_instance_private(o))

static void
fu_ifd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiFirmwareVolume *self = FU_EFI_FIRMWARE_VOLUME(firmware);
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "attrs", priv->attrs);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv(bn,
					  "name",
					  fu_efi_guid_to_name(fu_firmware_get_id(firmware)));
	}
}

static gboolean
fu_efi_firmware_volume_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_efi_volume_validate(g_bytes_get_data(fw, NULL),
					     g_bytes_get_size(fw),
					     offset,
					     error);
}

static gboolean
fu_efi_firmware_volume_parse(FuFirmware *firmware,
			     GBytes *fw,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuEfiFirmwareVolume *self = FU_EFI_FIRMWARE_VOLUME(firmware);
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE(self);
	gsize blockmap_sz = 0;
	gsize bufsz = 0;
	guint16 hdr_length = 0;
	guint32 attrs = 0;
	guint64 fv_length = 0;
	guint8 alignment;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *guid_str = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GByteArray) st_hdr = NULL;

	/* parse */
	st_hdr = fu_struct_efi_volume_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;

	/* guid */
	guid_str = fwupd_guid_to_string(fu_struct_efi_volume_get_guid(st_hdr),
					FWUPD_GUID_FLAG_MIXED_ENDIAN);
	g_debug("volume GUID: %s [%s]", guid_str, fu_efi_guid_to_name(guid_str));

	/* length */
	fv_length = fu_struct_efi_volume_get_length(st_hdr);
	if (fv_length == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid volume length");
		return FALSE;
	}
	fu_firmware_set_size(firmware, fv_length);
	attrs = fu_struct_efi_volume_get_attrs(st_hdr);
	alignment = (attrs & 0x00ff0000) >> 16;
	if (alignment > FU_FIRMWARE_ALIGNMENT_2G) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "0x%x invalid, maximum is 0x%x",
			    (guint)alignment,
			    (guint)FU_FIRMWARE_ALIGNMENT_2G);
		return FALSE;
	}
	fu_firmware_set_alignment(firmware, alignment);
	priv->attrs = attrs & 0xffff;
	hdr_length = fu_struct_efi_volume_get_hdr_len(st_hdr);
	if (hdr_length < st_hdr->len || hdr_length > fv_length || hdr_length > bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid volume header length");
		return FALSE;
	}

	/* verify checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint16 checksum_verify = fu_sum16w(buf, hdr_length, G_LITTLE_ENDIAN);
		if (checksum_verify != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum invalid, got %02x, expected %02x",
				    checksum_verify,
				    fu_struct_efi_volume_get_checksum(st_hdr));
			return FALSE;
		}
	}

	/* add image */
	blob = fu_bytes_new_offset(fw, offset + hdr_length, fv_length - hdr_length, error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_offset(firmware, offset);
	fu_firmware_set_id(firmware, guid_str);
	fu_firmware_set_size(firmware, fv_length);

	/* parse, which might cascade and do something like FFS2 */
	if (g_strcmp0(guid_str, FU_EFI_FIRMWARE_VOLUME_GUID_FFS2) == 0) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_filesystem_new();
		fu_firmware_set_alignment(img, fu_firmware_get_alignment(firmware));
		if (!fu_firmware_parse(img, blob, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error))
			return FALSE;
		fu_firmware_add_image(firmware, img);
	} else {
		fu_firmware_set_bytes(firmware, blob);
	}

	/* skip the blockmap */
	offset += st_hdr->len;
	while (offset < bufsz) {
		guint32 num_blocks;
		guint32 length;
		g_autoptr(GByteArray) st_blk = NULL;
		st_blk = fu_struct_efi_volume_block_map_parse(buf, bufsz, offset, error);
		if (st_blk == NULL)
			return FALSE;
		num_blocks = fu_struct_efi_volume_block_map_get_num_blocks(st_blk);
		length = fu_struct_efi_volume_block_map_get_length(st_blk);
		offset += st_blk->len;
		if (num_blocks == 0x0 && length == 0x0)
			break;
		blockmap_sz += (gsize)num_blocks * (gsize)length;
	}
	if (blockmap_sz < (gsize)fv_length) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "blocks allocated is less than volume length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_firmware_volume_write(FuFirmware *firmware, GError **error)
{
	FuEfiFirmwareVolume *self = FU_EFI_FIRMWARE_VOLUME(firmware);
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = fu_struct_efi_volume_new();
	g_autoptr(GByteArray) st_blk = fu_struct_efi_volume_block_map_new();
	fwupd_guid_t guid = {0x0};
	guint32 hdr_length = 0x48;
	guint64 fv_length;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(FuFirmware) img = NULL;

	/* sanity check */
	if (fu_firmware_get_alignment(firmware) > FU_FIRMWARE_ALIGNMENT_1M) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "alignment invalid, got 0x%02x",
			    fu_firmware_get_alignment(firmware));
		return NULL;
	}

	/* GUID */
	if (fu_firmware_get_id(firmware) == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no GUID set for EFI FV");
		return NULL;
	}
	if (!fwupd_guid_from_string(fu_firmware_get_id(firmware),
				    &guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error))
		return NULL;

	/* length */
	img = fu_firmware_get_image_by_id(firmware, NULL, NULL);
	if (img != NULL) {
		img_blob = fu_firmware_write(img, error);
		if (img_blob == NULL) {
			g_prefix_error(error, "no EFI FV child payload: ");
			return NULL;
		}
	} else {
		img_blob = fu_firmware_get_bytes_with_patches(firmware, error);
		if (img_blob == NULL) {
			g_prefix_error(error, "no EFI FV payload: ");
			return NULL;
		}
	}

	/* pack */
	fu_struct_efi_volume_set_guid(buf, &guid);
	fv_length = fu_common_align_up(hdr_length + g_bytes_get_size(img_blob),
				       fu_firmware_get_alignment(firmware));
	fu_struct_efi_volume_set_length(buf, fv_length);
	fu_struct_efi_volume_set_attrs(buf,
				       priv->attrs |
					   ((guint32)fu_firmware_get_alignment(firmware) << 16));
	fu_struct_efi_volume_set_hdr_len(buf, hdr_length);

	/* blockmap */
	fu_struct_efi_volume_block_map_set_num_blocks(st_blk, fv_length);
	fu_struct_efi_volume_block_map_set_length(st_blk, 0x1);
	g_byte_array_append(buf, st_blk->data, st_blk->len);
	fu_struct_efi_volume_block_map_set_num_blocks(st_blk, 0x0);
	fu_struct_efi_volume_block_map_set_length(st_blk, 0x0);
	g_byte_array_append(buf, st_blk->data, st_blk->len);

	/* fix up checksum */
	fu_struct_efi_volume_set_checksum(buf,
					  0x10000 -
					      fu_sum16w(buf->data, buf->len, G_LITTLE_ENDIAN));

	/* pad contents to alignment */
	fu_byte_array_append_bytes(buf, img_blob);
	fu_byte_array_set_size(buf, fv_length, 0xFF);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_efi_firmware_volume_init(FuEfiFirmwareVolume *self)
{
	FuEfiFirmwareVolumePrivate *priv = GET_PRIVATE(self);
	priv->attrs = 0xfeff;
}

static void
fu_efi_firmware_volume_class_init(FuEfiFirmwareVolumeClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_efi_firmware_volume_check_magic;
	klass_firmware->parse = fu_efi_firmware_volume_parse;
	klass_firmware->write = fu_efi_firmware_volume_write;
	klass_firmware->export = fu_ifd_firmware_export;
}

/**
 * fu_efi_firmware_volume_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.2
 **/
FuFirmware *
fu_efi_firmware_volume_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_FIRMWARE_VOLUME, NULL));
}
