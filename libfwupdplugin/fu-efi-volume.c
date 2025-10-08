/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiVolume"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-chunk-array.h"
#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-filesystem.h"
#include "fu-efi-ftw-store.h"
#include "fu-efi-struct.h"
#include "fu-efi-volume.h"
#include "fu-efi-vss2-variable-store.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-sum.h"

/**
 * FuEfiVolume:
 *
 * A UEFI file volume.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint16 attrs;
} FuEfiVolumePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiVolume, fu_efi_volume, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_volume_get_instance_private(o))

static void
fu_efi_volume_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiVolume *self = FU_EFI_VOLUME(firmware);
	FuEfiVolumePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "attrs", priv->attrs);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kv(bn,
					  "name",
					  fu_efi_guid_to_name(fu_firmware_get_id(firmware)));
	}
}

static gboolean
fu_efi_volume_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_efi_volume_validate_stream(stream, offset, error);
}

static gboolean
fu_efi_volume_parse_nvram_evsa(FuEfiVolume *self,
			       GInputStream *stream,
			       gsize offset,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	gsize streamsz = 0;
	guint found_cnt = 0;
	gsize offset_last = offset;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GError) error_local = NULL;

		/* try to find a NVRAM store */
		img = fu_firmware_new_from_gtypes(stream,
						  offset,
						  flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
						  &error_local,
						  FU_TYPE_EFI_VSS2_VARIABLE_STORE,
						  FU_TYPE_EFI_FTW_STORE,
						  G_TYPE_INVALID);
		if (img == NULL) {
			if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
				g_debug("ignoring EFI NVRAM @0x%x: %s",
					(guint)offset,
					error_local->message);
			}
			offset += 0x1000;
			continue;
		}

		/* sanity check */
		if (fu_firmware_get_size(img) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "NVRAM store entry has zero size");
			return FALSE;
		}

		/* preserve the exact padding between EVSA stores */
		if (offset != offset_last) {
			g_autoptr(GBytes) blob = g_bytes_new(NULL, 0);
			g_autoptr(GBytes) blob_padded =
			    fu_bytes_pad(blob, offset - offset_last, 0xFF);
			g_autoptr(FuFirmware) img_padded = fu_firmware_new_from_bytes(blob_padded);
			if (!fu_firmware_add_image(FU_FIRMWARE(self), img_padded, error))
				return FALSE;
		}

		/* we found something */
		fu_firmware_set_offset(img, offset);
		if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
			return FALSE;
		offset += fu_firmware_get_size(img);
		offset = fu_common_align_up(offset, FU_FIRMWARE_ALIGNMENT_4K);
		found_cnt += 1;

		/* the last thing we found */
		offset_last = offset;
	}

	/* we found nothing */
	if (found_cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no NVRAM stores found");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_volume_parse(FuFirmware *firmware,
		    GInputStream *stream,
		    FuFirmwareParseFlags flags,
		    GError **error)
{
	FuEfiVolume *self = FU_EFI_VOLUME(firmware);
	FuEfiVolumePrivate *priv = GET_PRIVATE(self);
	gsize blockmap_sz = 0;
	gsize offset = 0;
	gsize streamsz = 0;
	guint16 hdr_length = 0;
	guint32 attrs = 0;
	guint64 fv_length = 0;
	guint8 alignment;
	g_autofree gchar *guid_str = NULL;
	g_autoptr(FuStructEfiVolume) st_hdr = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* parse */
	st_hdr = fu_struct_efi_volume_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;

	/* guid */
	guid_str = fwupd_guid_to_string(fu_struct_efi_volume_get_guid(st_hdr),
					FWUPD_GUID_FLAG_MIXED_ENDIAN);
	g_debug("volume GUID: %s [%s]", guid_str, fu_efi_guid_to_name(guid_str));

	/* length */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	fv_length = fu_struct_efi_volume_get_length(st_hdr);
	if (fv_length == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid volume length");
		return FALSE;
	}
	if (fv_length > fu_firmware_get_size_max(firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "volume length larger than max size: 0x%x > 0x%x",
			    (guint)fv_length,
			    (guint)fu_firmware_get_size_max(firmware));
		return FALSE;
	}
	attrs = fu_struct_efi_volume_get_attrs(st_hdr);
	alignment = (attrs & 0x00ff0000) >> 16;
	if (alignment > FU_FIRMWARE_ALIGNMENT_2G) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "0x%x invalid, maximum is 0x%x",
			    (guint)alignment,
			    (guint)FU_FIRMWARE_ALIGNMENT_2G);
		return FALSE;
	}
	fu_firmware_set_alignment(firmware, alignment);
	priv->attrs = attrs & 0xffff;
	hdr_length = fu_struct_efi_volume_get_hdr_len(st_hdr);
	if (hdr_length < st_hdr->len || hdr_length > fv_length || hdr_length > streamsz ||
	    hdr_length % 2 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid volume header length 0x%x",
			    (guint)hdr_length);
		return FALSE;
	}

	/* verify checksum */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint16 checksum_verify;
		g_autoptr(GBytes) blob_hdr = NULL;

		blob_hdr = fu_input_stream_read_bytes(stream, 0x0, hdr_length, NULL, error);
		if (blob_hdr == NULL)
			return FALSE;
		checksum_verify = fu_sum16w_bytes(blob_hdr, G_LITTLE_ENDIAN);
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

	/* extended header items */
	if (fu_struct_efi_volume_get_ext_hdr(st_hdr) != 0) {
		g_autoptr(FuStructEfiVolumeExtHeader) st_ext_hdr = NULL;
		goffset offset_ext = fu_struct_efi_volume_get_ext_hdr(st_hdr);
		st_ext_hdr =
		    fu_struct_efi_volume_ext_header_parse_stream(stream, offset_ext, error);
		if (st_ext_hdr == NULL)
			return FALSE;
		offset_ext += fu_struct_efi_volume_ext_header_get_size(st_ext_hdr);
		do {
			g_autoptr(FuStructEfiVolumeExtEntry) st_ext_entry = NULL;
			st_ext_entry =
			    fu_struct_efi_volume_ext_entry_parse_stream(stream, offset_ext, error);
			if (st_ext_entry == NULL)
				return FALSE;
			if (fu_struct_efi_volume_ext_entry_get_size(st_ext_entry) == 0x0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "EFI_VOLUME_EXT_ENTRY invalid size");
				return FALSE;
			}
			if (fu_struct_efi_volume_ext_entry_get_size(st_ext_entry) == 0xFFFF)
				break;
			offset_ext += fu_struct_efi_volume_ext_entry_get_size(st_ext_entry);
		} while ((gsize)offset_ext < fv_length);
	}

	/* add image */
	partial_stream =
	    fu_partial_input_stream_new(stream, hdr_length, fv_length - hdr_length, error);
	if (partial_stream == NULL) {
		g_prefix_error_literal(error, "failed to cut EFI volume: ");
		return FALSE;
	}
	fu_firmware_set_id(firmware, guid_str);
	fu_firmware_set_size(firmware, fv_length);

	/* parse, which might cascade and do something like FFS2 */
	if (g_strcmp0(guid_str, FU_EFI_VOLUME_GUID_FFS2) == 0 ||
	    g_strcmp0(guid_str, FU_EFI_VOLUME_GUID_FFS3) == 0) {
		g_autoptr(FuFirmware) img = fu_efi_filesystem_new();
		fu_firmware_set_alignment(img, fu_firmware_get_alignment(firmware));
		if (!fu_firmware_parse_stream(img,
					      partial_stream,
					      0x0,
					      flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
					      error))
			return FALSE;
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;
	} else if (g_strcmp0(guid_str, FU_EFI_VOLUME_GUID_NVRAM_EVSA) == 0 ||
		   g_strcmp0(guid_str, FU_EFI_VOLUME_GUID_NVRAM_EVSA2) == 0) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_efi_volume_parse_nvram_evsa(self,
						    stream,
						    hdr_length,
						    flags,
						    &error_local)) {
			g_debug("ignoring %s [%s] EFI FV: %s",
				guid_str,
				fu_efi_guid_to_name(guid_str),
				error_local->message);
			if (!fu_firmware_set_stream(firmware, partial_stream, error))
				return FALSE;
		}
	} else {
		g_warning("no idea how to parse %s [%s] EFI volume",
			  guid_str,
			  fu_efi_guid_to_name(guid_str));
		if (!fu_firmware_set_stream(firmware, partial_stream, error))
			return FALSE;
	}

	/* skip the blockmap */
	offset += st_hdr->len;
	while (offset < streamsz) {
		guint32 num_blocks;
		guint32 length;
		g_autoptr(FuStructEfiVolumeBlockMap) st_blk = NULL;
		st_blk = fu_struct_efi_volume_block_map_parse_stream(stream, offset, error);
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

static GByteArray *
fu_efi_volume_write(FuFirmware *firmware, GError **error)
{
	FuEfiVolume *self = FU_EFI_VOLUME(firmware);
	FuEfiVolumePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuStructEfiVolume) buf = fu_struct_efi_volume_new();
	g_autoptr(FuStructEfiVolumeBlockMap) st_blk = fu_struct_efi_volume_block_map_new();
	fwupd_guid_t guid = {0x0};
	guint32 hdr_length = 0x48;
	guint64 fv_length;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GPtrArray) images = NULL;

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
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no GUID set for FV");
		return NULL;
	}
	if (!fwupd_guid_from_string(fu_firmware_get_id(firmware),
				    &guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error))
		return NULL;

	/* length */
	images = fu_firmware_get_images(firmware);
	if (images->len == 0) {
		img_blob = fu_firmware_get_bytes_with_patches(firmware, error);
		if (img_blob == NULL) {
			g_prefix_error_literal(error, "no EFI FV payload: ");
			return NULL;
		}
	} else {
		g_autoptr(GByteArray) buf_tmp = g_byte_array_new();
		for (guint i = 0; i < images->len; i++) {
			FuFirmware *img = g_ptr_array_index(images, i);
			g_autoptr(GBytes) img_blob_tmp = NULL;

			img_blob_tmp = fu_firmware_write(img, error);
			if (img_blob_tmp == NULL) {
				g_prefix_error_literal(error, "no EFI FV child payload: ");
				return NULL;
			}
			fu_byte_array_append_bytes(buf_tmp, img_blob_tmp);
		}
		img_blob =
		    g_byte_array_free_to_bytes(g_steal_pointer(&buf_tmp)); /* nocheck:blocked */
	}

	/* pack */
	fu_struct_efi_volume_set_guid(buf, &guid);
	fv_length = fu_common_align_up(hdr_length + g_bytes_get_size(img_blob),
				       fu_firmware_get_alignment(firmware));

	/* we want a minimum size of volume */
	if (fu_firmware_get_size(firmware) > fv_length) {
		g_debug("padding FV from 0x%x to 0x%x",
			(guint)fv_length,
			(guint)fu_firmware_get_size(firmware));
		fv_length = fu_firmware_get_size(firmware);
	}

	fu_struct_efi_volume_set_length(buf, fv_length);
	fu_struct_efi_volume_set_attrs(buf,
				       priv->attrs |
					   ((guint32)fu_firmware_get_alignment(firmware) << 16));
	fu_struct_efi_volume_set_hdr_len(buf, hdr_length);

	/* blockmap */
	chunks = fu_chunk_array_new_virtual(fv_length,
					    FU_CHUNK_ADDR_OFFSET_NONE,
					    FU_CHUNK_PAGESZ_NONE,
					    0x1000);
	fu_struct_efi_volume_block_map_set_num_blocks(st_blk, fu_chunk_array_length(chunks));
	fu_struct_efi_volume_block_map_set_length(st_blk, 0x1000);
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
	return g_steal_pointer(&buf);
}

static void
fu_efi_volume_init(FuEfiVolume *self)
{
	FuEfiVolumePrivate *priv = GET_PRIVATE(self);
	priv->attrs = 0xfeff;
#ifdef HAVE_FUZZER
	fu_firmware_set_size_max(FU_FIRMWARE(self), 0x100000); /* 1MB */
	fu_firmware_set_images_max(FU_FIRMWARE(self), 10);
#else
	fu_firmware_set_size_max(FU_FIRMWARE(self), 0x10000000); /* 256MB */
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1000);
#endif
	g_type_ensure(FU_TYPE_EFI_FILESYSTEM);
	g_type_ensure(FU_TYPE_EFI_VSS2_VARIABLE_STORE);
	g_type_ensure(FU_TYPE_EFI_FTW_STORE);
}

static void
fu_efi_volume_class_init(FuEfiVolumeClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_efi_volume_validate;
	firmware_class->parse = fu_efi_volume_parse;
	firmware_class->write = fu_efi_volume_write;
	firmware_class->export = fu_efi_volume_export;
}

/**
 * fu_efi_volume_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 2.0.0
 **/
FuFirmware *
fu_efi_volume_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_VOLUME, NULL));
}
