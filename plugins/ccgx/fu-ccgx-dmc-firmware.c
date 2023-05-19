/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-ccgx-dmc-firmware.h"
#include "fu-ccgx-dmc-struct.h"

struct _FuCcgxDmcFirmware {
	FuFirmwareClass parent_instance;
	GPtrArray *image_records;
	GBytes *fwct_blob;
	GBytes *custom_meta_blob;
	guint32 row_data_offset_start;
	guint32 fw_data_size;
};

G_DEFINE_TYPE(FuCcgxDmcFirmware, fu_ccgx_dmc_firmware, FU_TYPE_FIRMWARE)

#define DMC_FWCT_MAX_SIZE		  2048
#define DMC_HASH_SIZE			  32
#define DMC_CUSTOM_META_LENGTH_FIELD_SIZE 2

static void
fu_ccgx_dmc_firmware_record_free(FuCcgxDmcFirmwareRecord *rcd)
{
	if (rcd->seg_records != NULL)
		g_ptr_array_unref(rcd->seg_records);
	g_free(rcd);
}

static void
fu_ccgx_dmc_firmware_segment_record_free(FuCcgxDmcFirmwareSegmentRecord *rcd)
{
	if (rcd->data_records != NULL)
		g_ptr_array_unref(rcd->data_records);
	g_free(rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxDmcFirmwareRecord, fu_ccgx_dmc_firmware_record_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxDmcFirmwareSegmentRecord,
			      fu_ccgx_dmc_firmware_segment_record_free)

GPtrArray *
fu_ccgx_dmc_firmware_get_image_records(FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_DMC_FIRMWARE(self), NULL);
	return self->image_records;
}

GBytes *
fu_ccgx_dmc_firmware_get_fwct_record(FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_DMC_FIRMWARE(self), NULL);
	return self->fwct_blob;
}

GBytes *
fu_ccgx_dmc_firmware_get_custom_meta_record(FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_DMC_FIRMWARE(self), NULL);
	return self->custom_meta_blob;
}

guint32
fu_ccgx_dmc_firmware_get_fw_data_size(FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_DMC_FIRMWARE(self), 0);
	return self->fw_data_size;
}

static void
fu_ccgx_dmc_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE(firmware);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kx(bn, "fw_data_size", self->fw_data_size);
		fu_xmlb_builder_insert_kx(bn, "image_records", self->image_records->len);
	}
}

static gboolean
fu_ccgx_dmc_firmware_parse_segment(FuFirmware *firmware,
				   const guint8 *buf,
				   gsize bufsz,
				   FuCcgxDmcFirmwareRecord *img_rcd,
				   gsize *seg_off,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE(firmware);
	gsize row_off;
	g_autoptr(GChecksum) csum = g_checksum_new(G_CHECKSUM_SHA256);

	/* set row data offset in current image */
	row_off = self->row_data_offset_start + img_rcd->img_offset;

	/* parse segment in image */
	img_rcd->seg_records =
	    g_ptr_array_new_with_free_func((GFreeFunc)fu_ccgx_dmc_firmware_segment_record_free);
	for (guint32 i = 0; i < img_rcd->num_img_segments; i++) {
		guint16 row_size_bytes = 0;
		g_autofree guint8 *row_buf = NULL;
		g_autoptr(FuCcgxDmcFirmwareSegmentRecord) seg_rcd = NULL;
		g_autoptr(GByteArray) st_info = NULL;

		/* read segment info  */
		seg_rcd = g_new0(FuCcgxDmcFirmwareSegmentRecord, 1);
		st_info =
		    fu_struct_ccgx_dmc_fwct_segmentation_info_parse(buf, bufsz, *seg_off, error);
		if (st_info == NULL)
			return FALSE;
		seg_rcd->start_row =
		    fu_struct_ccgx_dmc_fwct_segmentation_info_get_start_row(st_info);
		seg_rcd->num_rows = fu_struct_ccgx_dmc_fwct_segmentation_info_get_num_rows(st_info);

		/* calculate actual row size */
		row_size_bytes = img_rcd->row_size * 64;

		/* create data record array in segment record */
		seg_rcd->data_records =
		    g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);

		/* read row data in segment */
		row_buf = g_malloc0(row_size_bytes);
		for (int row = 0; row < seg_rcd->num_rows; row++) {
			g_autoptr(GBytes) data_rcd = NULL;

			/* read row data */
			if (!fu_memcpy_safe(row_buf,
					    row_size_bytes,
					    0x0, /* dst */
					    buf,
					    bufsz,
					    row_off, /* src */
					    row_size_bytes,
					    error)) {
				g_prefix_error(error, "failed to read row data: ");
				return FALSE;
			}

			/* update hash */
			g_checksum_update(csum, (guchar *)row_buf, row_size_bytes);

			/* add row data to data record */
			data_rcd = g_bytes_new(row_buf, row_size_bytes);
			g_ptr_array_add(seg_rcd->data_records, g_steal_pointer(&data_rcd));

			/* increment row data offset */
			row_off += row_size_bytes;
		}

		/* add segment record to segment array */
		g_ptr_array_add(img_rcd->seg_records, g_steal_pointer(&seg_rcd));

		/* increment segment info offset */
		*seg_off += st_info->len;
	}

	/* check checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 csumbuf[DMC_HASH_SIZE] = {0x0};
		gsize csumbufsz = sizeof(csumbuf);
		g_checksum_get_digest(csum, csumbuf, &csumbufsz);
		if (memcmp(csumbuf, img_rcd->img_digest, DMC_HASH_SIZE) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid hash");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_dmc_firmware_parse_image(FuFirmware *firmware,
				 guint8 image_count,
				 const guint8 *buf,
				 gsize bufsz,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE(firmware);
	gsize img_off = FU_STRUCT_CCGX_DMC_FWCT_INFO_SIZE;
	gsize seg_off = FU_STRUCT_CCGX_DMC_FWCT_INFO_SIZE +
			image_count * FU_STRUCT_CCGX_DMC_FWCT_IMAGE_INFO_SIZE;

	/* set initial segment info offset */
	for (guint32 i = 0; i < image_count; i++) {
		gsize img_digestsz = 0;
		const guint8 *img_digest;
		g_autoptr(FuCcgxDmcFirmwareRecord) img_rcd = NULL;
		g_autoptr(GByteArray) st_img = NULL;

		/* read image info */
		img_rcd = g_new0(FuCcgxDmcFirmwareRecord, 1);
		st_img = fu_struct_ccgx_dmc_fwct_image_info_parse(buf, bufsz, img_off, error);
		if (st_img == NULL)
			return FALSE;
		img_rcd->row_size = fu_struct_ccgx_dmc_fwct_image_info_get_row_size(st_img);
		if (img_rcd->row_size == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid row size 0x%x",
				    img_rcd->row_size);
			return FALSE;
		}
		img_rcd->img_offset = fu_struct_ccgx_dmc_fwct_image_info_get_img_offset(st_img);
		img_rcd->num_img_segments =
		    fu_struct_ccgx_dmc_fwct_image_info_get_num_img_segments(st_img);
		if (img_rcd->num_img_segments == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid segment number = %d",
				    img_rcd->num_img_segments);
			return FALSE;
		}
		img_digest =
		    fu_struct_ccgx_dmc_fwct_image_info_get_img_digest(st_img, &img_digestsz);
		if (!fu_memcpy_safe((guint8 *)&img_rcd->img_digest,
				    sizeof(img_rcd->img_digest),
				    0x0, /* dst */
				    img_digest,
				    img_digestsz,
				    0, /* src */
				    img_digestsz,
				    error))
			return FALSE;

		/* parse segment */
		if (!fu_ccgx_dmc_firmware_parse_segment(firmware,
							buf,
							bufsz,
							img_rcd,
							&seg_off,
							flags,
							error))
			return FALSE;

		/* add image record to image record array */
		g_ptr_array_add(self->image_records, g_steal_pointer(&img_rcd));

		/* increment image offset */
		img_off += FU_STRUCT_CCGX_DMC_FWCT_IMAGE_INFO_SIZE;
	}

	return TRUE;
}

static gboolean
fu_ccgx_dmc_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_ccgx_dmc_fwct_info_validate(g_bytes_get_data(fw, NULL),
						     g_bytes_get_size(fw),
						     offset,
						     error);
}

static gboolean
fu_ccgx_dmc_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint16 hdr_size = 0;
	guint16 mdbufsz = 0;
	guint32 hdr_composite_version = 0;
	guint8 hdr_image_count = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(FuFirmware) img = fu_firmware_new_from_bytes(fw);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* parse */
	st_hdr = fu_struct_ccgx_dmc_fwct_info_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;

	/* check fwct size */
	hdr_size = fu_struct_ccgx_dmc_fwct_info_get_size(st_hdr);
	if (hdr_size > DMC_FWCT_MAX_SIZE || hdr_size == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid dmc fwct size, expected <= 0x%x, got 0x%x",
			    (guint)DMC_FWCT_MAX_SIZE,
			    (guint)hdr_size);
		return FALSE;
	}

	/* set version */
	hdr_composite_version = fu_struct_ccgx_dmc_fwct_info_get_composite_version(st_hdr);
	if (hdr_composite_version != 0) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint32(hdr_composite_version, FWUPD_VERSION_FORMAT_QUAD);
		fu_firmware_set_version(firmware, ver);
		fu_firmware_set_version_raw(firmware, hdr_composite_version);
	}

	/* read fwct data */
	self->fwct_blob = fu_bytes_new_offset(fw, offset, hdr_size, error);
	if (self->fwct_blob == NULL)
		return FALSE;

	/* create custom meta binary */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + hdr_size,
				    &mdbufsz,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read metadata size: ");
		return FALSE;
	}
	if (mdbufsz > 0) {
		self->custom_meta_blob =
		    fu_bytes_new_offset(fw, offset + hdr_size + 2, mdbufsz, error);
		if (self->custom_meta_blob == NULL)
			return FALSE;
	}

	/* set row data start offset */
	self->row_data_offset_start = hdr_size + DMC_CUSTOM_META_LENGTH_FIELD_SIZE + mdbufsz;
	self->fw_data_size = bufsz - self->row_data_offset_start;

	/* parse image */
	hdr_image_count = fu_struct_ccgx_dmc_fwct_info_get_image_count(st_hdr);
	if (!fu_ccgx_dmc_firmware_parse_image(firmware, hdr_image_count, buf, bufsz, flags, error))
		return FALSE;

	/* add something, although we'll use the records for the update */
	fu_firmware_set_addr(img, 0x0);
	fu_firmware_add_image(firmware, img);
	return TRUE;
}

static GByteArray *
fu_ccgx_dmc_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_hdr = fu_struct_ccgx_dmc_fwct_info_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* add header */
	fu_struct_ccgx_dmc_fwct_info_set_size(
	    st_hdr,
	    FU_STRUCT_CCGX_DMC_FWCT_INFO_SIZE +
		(images->len * (FU_STRUCT_CCGX_DMC_FWCT_IMAGE_INFO_SIZE +
				FU_STRUCT_CCGX_DMC_FWCT_SEGMENTATION_INFO_SIZE)));
	fu_struct_ccgx_dmc_fwct_info_set_version(st_hdr, 0x2);
	fu_struct_ccgx_dmc_fwct_info_set_custom_meta_type(st_hdr, 0x3);
	fu_struct_ccgx_dmc_fwct_info_set_cdtt_version(st_hdr, 0x1);
	fu_struct_ccgx_dmc_fwct_info_set_device_id(st_hdr, 0x1);
	fu_struct_ccgx_dmc_fwct_info_set_composite_version(st_hdr,
							   fu_firmware_get_version_raw(firmware));
	fu_struct_ccgx_dmc_fwct_info_set_image_count(st_hdr, images->len);
	g_byte_array_append(buf, st_hdr->data, st_hdr->len);

	/* add image headers */
	for (guint i = 0; i < images->len; i++) {
		g_autoptr(GByteArray) st_img = fu_struct_ccgx_dmc_fwct_image_info_new();
		fu_struct_ccgx_dmc_fwct_image_info_set_device_type(st_img, 0x2);
		fu_struct_ccgx_dmc_fwct_image_info_set_img_type(st_img, 0x1);
		fu_struct_ccgx_dmc_fwct_image_info_set_row_size(st_img, 0x1);
		fu_struct_ccgx_dmc_fwct_image_info_set_fw_version(st_img, 0x330006d2);
		fu_struct_ccgx_dmc_fwct_image_info_set_app_version(st_img, 0x14136161);
		fu_struct_ccgx_dmc_fwct_image_info_set_num_img_segments(st_img, 0x1);
		g_byte_array_append(buf, st_img->data, st_img->len);
	}

	/* add segments */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GByteArray) st_info = fu_struct_ccgx_dmc_fwct_segmentation_info_new();
		g_autoptr(GPtrArray) chunks = NULL;
		g_autoptr(GBytes) img_bytes = fu_firmware_get_bytes(img, error);
		if (img_bytes == NULL)
			return NULL;
		chunks = fu_chunk_array_new_from_bytes(img_bytes, 0x0, 0x0, 64);
		fu_struct_ccgx_dmc_fwct_segmentation_info_set_num_rows(st_info,
								       MAX(chunks->len, 1));
		g_byte_array_append(buf, st_info->data, st_info->len);
	}

	/* metadata */
	fu_byte_array_append_uint16(buf, 0x1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, 0xff);

	/* add image headers */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		gsize csumbufsz = DMC_HASH_SIZE;
		gsize img_offset = FU_STRUCT_CCGX_DMC_FWCT_INFO_SIZE +
				   (i * FU_STRUCT_CCGX_DMC_FWCT_IMAGE_INFO_SIZE);
		guint8 csumbuf[DMC_HASH_SIZE] = {0x0};
		g_autoptr(GChecksum) csum = g_checksum_new(G_CHECKSUM_SHA256);
		g_autoptr(GBytes) img_bytes = NULL;
		g_autoptr(GBytes) img_padded = NULL;
		g_autoptr(GPtrArray) chunks = NULL;

		img_bytes = fu_firmware_get_bytes(img, error);
		if (img_bytes == NULL)
			return NULL;
		chunks = fu_chunk_array_new_from_bytes(img_bytes, 0x0, 0x0, 64);
		img_padded = fu_bytes_pad(img_bytes, MAX(chunks->len, 1) * 64);
		fu_byte_array_append_bytes(buf, img_padded);
		g_checksum_update(csum,
				  (const guchar *)g_bytes_get_data(img_padded, NULL),
				  g_bytes_get_size(img_padded));
		g_checksum_get_digest(csum, csumbuf, &csumbufsz);

		/* update checksum */
		if (!fu_memcpy_safe(buf->data,
				    buf->len, /* dst */
				    img_offset +
					FU_STRUCT_CCGX_DMC_FWCT_IMAGE_INFO_OFFSET_IMG_DIGEST,
				    csumbuf,
				    sizeof(csumbuf),
				    0x0, /* src */
				    sizeof(csumbuf),
				    error))
			return NULL;
	}

	return g_steal_pointer(&buf);
}

static void
fu_ccgx_dmc_firmware_init(FuCcgxDmcFirmware *self)
{
	self->image_records =
	    g_ptr_array_new_with_free_func((GFreeFunc)fu_ccgx_dmc_firmware_record_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_ccgx_dmc_firmware_finalize(GObject *object)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE(object);

	if (self->fwct_blob != NULL)
		g_bytes_unref(self->fwct_blob);
	if (self->custom_meta_blob != NULL)
		g_bytes_unref(self->custom_meta_blob);
	if (self->image_records != NULL)
		g_ptr_array_unref(self->image_records);

	G_OBJECT_CLASS(fu_ccgx_dmc_firmware_parent_class)->finalize(object);
}

static void
fu_ccgx_dmc_firmware_class_init(FuCcgxDmcFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_ccgx_dmc_firmware_finalize;
	klass_firmware->check_magic = fu_ccgx_dmc_firmware_check_magic;
	klass_firmware->parse = fu_ccgx_dmc_firmware_parse;
	klass_firmware->write = fu_ccgx_dmc_firmware_write;
	klass_firmware->export = fu_ccgx_dmc_firmware_export;
}

FuFirmware *
fu_ccgx_dmc_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CCGX_DMC_FIRMWARE, NULL));
}
