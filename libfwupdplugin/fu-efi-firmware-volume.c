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
#include "fu-mem.h"
#include "fu-struct.h"
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

#define FU_EFI_FIRMWARE_VOLUME_REVISION	 0x02

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
	FuStruct *st_hdr = fu_struct_lookup(firmware, "EfiFirmwareVolumeHdr");
	return fu_struct_unpack_full(st_hdr,
				     g_bytes_get_data(fw, NULL),
				     g_bytes_get_size(fw),
				     offset,
				     FU_STRUCT_FLAG_ONLY_CONSTANTS,
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
	FuStruct *st_hdr = fu_struct_lookup(firmware, "EfiFirmwareVolumeHdr");
	FuStruct *st_blk = fu_struct_lookup(firmware, "EfiFirmwareVolumeBlockMap");
	gsize blockmap_sz = 0;
	gsize bufsz = 0;
	guint16 hdr_length = 0;
	guint32 attrs = 0;
	guint64 fv_length = 0;
	guint8 alignment;
	guint8 revision = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *guid_str = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* parse */
	if (!fu_struct_unpack_full(st_hdr, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;

	/* guid */
	guid_str =
	    fwupd_guid_to_string(fu_struct_get_guid(st_hdr, "guid"), FWUPD_GUID_FLAG_MIXED_ENDIAN);
	g_debug("volume GUID: %s [%s]", guid_str, fu_efi_guid_to_name(guid_str));

	/* length */
	fv_length = fu_struct_get_u64(st_hdr, "length");
	if (fv_length == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid volume length");
		return FALSE;
	}
	fu_firmware_set_size(firmware, fv_length);
	attrs = fu_struct_get_u32(st_hdr, "attrs");
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
	hdr_length = fu_struct_get_u16(st_hdr, "hdr_len");
	if (hdr_length < fu_struct_size(st_hdr) || hdr_length > fv_length || hdr_length > bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid volume header length");
		return FALSE;
	}
	revision = fu_struct_get_u8(st_hdr, "revision");
	if (revision != FU_EFI_FIRMWARE_VOLUME_REVISION) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "revision invalid, got 0x%x, expected 0x%x",
			    revision,
			    (guint)FU_EFI_FIRMWARE_VOLUME_REVISION);
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
				    fu_struct_get_u16(st_hdr, "checksum"));
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
	offset += fu_struct_size(st_hdr);
	while (offset < bufsz) {
		guint32 num_blocks;
		guint32 length;
		if (!fu_struct_unpack_full(st_blk, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
			return FALSE;
		num_blocks = fu_struct_get_u32(st_blk, "num_blocks");
		length = fu_struct_get_u32(st_blk, "length");
		offset += fu_struct_size(st_blk);
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
	FuStruct *st_hdr = fu_struct_lookup(firmware, "EfiFirmwareVolumeHdr");
	FuStruct *st_blk = fu_struct_lookup(firmware, "EfiFirmwareVolumeBlockMap");
	g_autoptr(GByteArray) buf = NULL;
	fwupd_guid_t guid = {0x0};
	guint16 checksum = 0;
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
	fu_struct_set_guid(st_hdr, "guid", &guid);
	fv_length = fu_common_align_up(hdr_length + g_bytes_get_size(img_blob),
				       fu_firmware_get_alignment(firmware));
	fu_struct_set_u64(st_hdr, "length", fv_length);
	fu_struct_set_u32(st_hdr,
			  "attrs",
			  priv->attrs | ((guint32)fu_firmware_get_alignment(firmware) << 16));
	fu_struct_set_u16(st_hdr, "hdr_len", hdr_length);
	buf = fu_struct_pack(st_hdr);

	/* blockmap */
	fu_struct_set_u32(st_blk, "num_blocks", fv_length);
	fu_struct_set_u32(st_blk, "length", 0x1);
	fu_struct_pack_into(st_blk, buf);
	fu_struct_set_u32(st_blk, "num_blocks", 0x0);
	fu_struct_set_u32(st_blk, "length", 0x0);
	fu_struct_pack_into(st_blk, buf);

	/* fix up checksum */
	checksum = fu_sum16w(buf->data, buf->len, G_LITTLE_ENDIAN);
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     fu_struct_get_id_offset(st_hdr, "checksum"),
				     0x10000 - checksum,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;

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
	fu_struct_register(self,
			   "EfiFirmwareVolumeHdr {"
			   "    zero_vector: guid,"
			   "    guid: guid,"
			   "    length: u64le,"
			   "    signature: u32le:: 0x4856465F,"
			   "    attrs: u32le,"
			   "    hdr_len: u16le,"
			   "    checksum: u16le,"
			   "    ext_hdr: u16le,"
			   "    reserved: u8,"
			   "    revision: u8: 0x02,"
			   "}");
	fu_struct_register(self,
			   "EfiFirmwareVolumeBlockMap {"
			   "    num_blocks: u32le,"
			   "    length: u32le,"
			   "}");
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
