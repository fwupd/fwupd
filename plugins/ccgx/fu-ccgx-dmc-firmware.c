/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-dmc-common.h"
#include "fu-ccgx-dmc-firmware.h"

struct _FuCcgxDmcFirmware {
	FuFirmwareClass parent_instance;
	GPtrArray *image_records;
	GBytes *fwct_blob;
	GBytes *custom_meta_blob;
	guint32 row_data_offset_start;
	guint32 fw_data_size;
};

G_DEFINE_TYPE(FuCcgxDmcFirmware, fu_ccgx_dmc_firmware, FU_TYPE_FIRMWARE)

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

		/* read segment info  */
		seg_rcd = g_new0(FuCcgxDmcFirmwareSegmentRecord, 1);
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    *seg_off +
						G_STRUCT_OFFSET(FwctSegmentationInfo, start_row),
					    &seg_rcd->start_row,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    *seg_off +
						G_STRUCT_OFFSET(FwctSegmentationInfo, num_rows),
					    &seg_rcd->num_rows,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

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
		*seg_off += sizeof(FwctSegmentationInfo);
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
	gsize img_off = sizeof(FwctInfo);
	gsize seg_off = sizeof(FwctInfo) + image_count * sizeof(FwctImageInfo);

	/* set initial segment info offset */
	for (guint32 i = 0; i < image_count; i++) {
		g_autoptr(FuCcgxDmcFirmwareRecord) img_rcd = NULL;

		/* read image info */
		img_rcd = g_new0(FuCcgxDmcFirmwareRecord, 1);
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   img_off + G_STRUCT_OFFSET(FwctImageInfo, row_size),
					   &img_rcd->row_size,
					   error))
			return FALSE;
		if (img_rcd->row_size == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid row size 0x%x",
				    img_rcd->row_size);
			return FALSE;
		}
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    img_off + G_STRUCT_OFFSET(FwctImageInfo, img_offset),
					    &img_rcd->img_offset,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   img_off +
					       G_STRUCT_OFFSET(FwctImageInfo, num_img_segments),
					   &img_rcd->num_img_segments,
					   error))
			return FALSE;
		if (img_rcd->num_img_segments == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid segment number = %d",
				    img_rcd->num_img_segments);
			return FALSE;
		}
		if (!fu_memcpy_safe((guint8 *)&img_rcd->img_digest,
				    sizeof(img_rcd->img_digest),
				    0x0, /* dst */
				    buf,
				    bufsz,
				    img_off + G_STRUCT_OFFSET(FwctImageInfo, img_digest), /* src */
				    sizeof(img_rcd->img_digest),
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
		img_off += sizeof(FwctImageInfo);
	}

	return TRUE;
}

static gboolean
fu_ccgx_dmc_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint32 magic = 0;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != DMC_FWCT_SIGN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid signature, expected 0x%04X got 0x%04X",
			    (guint32)DMC_FWCT_SIGN,
			    (guint32)magic);
		return FALSE;
	}

	/* success */
	return TRUE;
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

	/* check fwct size */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FwctInfo, size),
				    &hdr_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
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
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FwctInfo, composite_version),
				    &hdr_composite_version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
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
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FwctInfo, image_count),
				   &hdr_image_count,
				   error))
		return FALSE;
	if (!fu_ccgx_dmc_firmware_parse_image(firmware, hdr_image_count, buf, bufsz, flags, error))
		return FALSE;

	/* add something, although we'll use the records for the update */
	fu_firmware_set_addr(img, 0x0);
	fu_firmware_add_image(firmware, img);
	return TRUE;
}

static GBytes *
fu_ccgx_dmc_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* add header */
	fu_byte_array_append_uint32(buf, DMC_FWCT_SIGN, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(
	    buf,
	    sizeof(FwctInfo) +
		(images->len * (sizeof(FwctImageInfo) + sizeof(FwctSegmentationInfo))), /* size */
	    G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, 0x0);			/* checksum, unused */
	fu_byte_array_append_uint8(buf, 0x2);			/* version */
	fu_byte_array_append_uint8(buf, 0x3);			/* custom_meta_type */
	fu_byte_array_append_uint8(buf, 0x1);			/* cdtt_version */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* vid, unused */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* pid, unused */
	fu_byte_array_append_uint16(buf, 0x1, G_LITTLE_ENDIAN); /* device_id */
	for (guint j = 0; j < 16; j++)
		fu_byte_array_append_uint8(buf, 0x0); /* reserv0 */
	fu_byte_array_append_uint32(buf, fu_firmware_get_version_raw(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, images->len);
	for (guint j = 0; j < 3; j++)
		fu_byte_array_append_uint8(buf, 0x0); /* reserv1 */

	/* add image headers */
	for (guint i = 0; i < images->len; i++) {
		fu_byte_array_append_uint8(buf, 0x2); /* device_type, unknown */
		fu_byte_array_append_uint8(buf, 0x1); /* img_type, unknown */
		fu_byte_array_append_uint8(buf, 0x0); /* comp_id, unknown */
		fu_byte_array_append_uint8(buf, 0x1); /* row_size, multiplier for num_rows */
		for (guint j = 0; j < 4; j++)
			fu_byte_array_append_uint8(buf, 0x0); /* reserv0 */
		fu_byte_array_append_uint32(buf,
					    0x330006d2,
					    G_LITTLE_ENDIAN); /* fw_version, hardcoded */
		fu_byte_array_append_uint32(buf,
					    0x14136161,
					    G_LITTLE_ENDIAN);		/* app_version, hardcoded */
		fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* start of element data */
		fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* img_size */
		for (guint j = 0; j < 32; j++)
			fu_byte_array_append_uint8(buf, 0x0); /* img_digest */
		fu_byte_array_append_uint8(buf, 0x1);	      /* num_img_segments */
		for (guint j = 0; j < 3; j++)
			fu_byte_array_append_uint8(buf, 0x0); /* reserv1 */
	}

	/* add segments */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GPtrArray) chunks = NULL;
		g_autoptr(GBytes) img_bytes = fu_firmware_get_bytes(img, error);
		if (img_bytes == NULL)
			return NULL;
		chunks = fu_chunk_array_new_from_bytes(img_bytes, 0x0, 0x0, 64);
		fu_byte_array_append_uint8(buf, 0x0);			/* img_id */
		fu_byte_array_append_uint8(buf, 0x0);			/* type */
		fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* start_row, unknown */
		fu_byte_array_append_uint16(buf,
					    MAX(chunks->len, 1),
					    G_LITTLE_ENDIAN); /* num_rows */
		for (guint j = 0; j < 2; j++)
			fu_byte_array_append_uint8(buf, 0x0); /* reserv0 */
	}

	/* metadata */
	fu_byte_array_append_uint16(buf, 0x1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, 0xff);

	/* add image headers */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		gsize csumbufsz = DMC_HASH_SIZE;
		gsize img_offset = sizeof(FwctInfo) + (i * sizeof(FwctImageInfo));
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
				    img_offset + G_STRUCT_OFFSET(FwctImageInfo, img_digest),
				    csumbuf,
				    sizeof(csumbuf),
				    0x0, /* src */
				    sizeof(csumbuf),
				    error))
			return NULL;
	}

	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
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
