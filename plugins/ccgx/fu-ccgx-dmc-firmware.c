/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-common-version.h"
#include "fu-ccgx-dmc-common.h"
#include "fu-ccgx-common.h"
#include "fu-ccgx-dmc-firmware.h"

struct _FuCcgxDmcFirmware {
	FuFirmwareClass		 parent_instance;
	GPtrArray		*image_records;
	GBytes			*fwct_blob;
	GBytes			*custom_meta_blob;
	guint32			 row_data_offset_start;
	FwctInfo		 fwct_info;
	guint32			 fw_data_size;
};

G_DEFINE_TYPE (FuCcgxDmcFirmware, fu_ccgx_dmc_firmware, FU_TYPE_FIRMWARE)

static void
fu_ccgx_dmc_firmware_image_record_free (FuCcgxDmcFirmwareImageRecord *rcd)
{
	if (rcd->seg_records != NULL)
		g_ptr_array_unref (rcd->seg_records);
	g_free (rcd);
}

static void
fu_ccgx_dmc_firmware_segment_record_free (FuCcgxDmcFirmwareSegmentRecord *rcd)
{
	if (rcd->data_records != NULL)
		g_ptr_array_unref (rcd->data_records);
	g_free (rcd);
}

static void
fu_ccgx_dmc_firmware_data_record_free (FuCcgxDmcFirmwareDataRecord *rcd)
{
	if (rcd->data != NULL)
		g_bytes_unref (rcd->data);
	g_free (rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxDmcFirmwareImageRecord, fu_ccgx_dmc_firmware_image_record_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxDmcFirmwareSegmentRecord, fu_ccgx_dmc_firmware_segment_record_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxDmcFirmwareDataRecord, fu_ccgx_dmc_firmware_data_record_free)

GPtrArray *
fu_ccgx_dmc_firmware_get_image_records (FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_DMC_FIRMWARE (self), NULL);
	return self->image_records;
}

GBytes *
fu_ccgx_dmc_firmware_get_fwct_record (FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_DMC_FIRMWARE (self), NULL);
	return self->fwct_blob;
}

GBytes *
fu_ccgx_dmc_firmware_get_custom_meta_record (FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_DMC_FIRMWARE (self), NULL);
	return self->custom_meta_blob;
}

FwctInfo *
fu_ccgx_dmc_firmware_get_fwct_info (FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_DMC_FIRMWARE (self), NULL);
	return &self->fwct_info;
}

guint32
fu_ccgx_dmc_firmware_get_fw_data_size (FuCcgxDmcFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_DMC_FIRMWARE (self), 0);
	return self->fw_data_size;
}

static void
fu_ccgx_dmc_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE (firmware);
	fu_common_string_append_kx (str, idt, "FwDataSize", self->fw_data_size);
	fu_common_string_append_ku (str, idt, "ImageRecords", self->image_records->len);
}

static gboolean
fu_ccgx_dmc_firmware_parse_segment (FuFirmware *firmware,
				    const guint8 *fw_buf,
				    gsize fw_bufsz,
				    FuCcgxDmcFirmwareImageRecord *img_rcd,
				    gsize *segment_info_offset,
				    GError **error)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE (firmware);
	gsize fw_buffer_offset = 0;
	gsize row_data_offset;
	gsize digestlen = DMC_HASH_SIZE;
	guint8 hash[DMC_HASH_SIZE];
	g_autoptr(GChecksum) csum = g_checksum_new (G_CHECKSUM_SHA256);

	/* set row data offset in current image */
	row_data_offset = self->row_data_offset_start + img_rcd->info_header.img_offset;

	/* create segment record array in image record */
	img_rcd->seg_records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_ccgx_dmc_firmware_segment_record_free);

	/* parse segment in image */
	for (guint32 seg_num = 0; seg_num < img_rcd->info_header.num_img_segments; seg_num++) {
		guint16 actual_row_size = 0;
		g_autofree guint8 *row_buf = NULL;
		g_autoptr(FuCcgxDmcFirmwareSegmentRecord) seg_rcd = NULL;

		/* set segment info offset */
		fw_buffer_offset = *segment_info_offset;

		/* read segment info  */
		seg_rcd = g_new0 (FuCcgxDmcFirmwareSegmentRecord, 1);
		if (!fu_memcpy_safe ((guint8 *) &seg_rcd->info_header, sizeof(seg_rcd->info_header), 0x0, /* dst */
				     fw_buf, fw_bufsz, fw_buffer_offset, /* src */
				     sizeof(seg_rcd->info_header), error))
			return FALSE;

		/* calculate actual row size */
		actual_row_size = img_rcd->info_header.row_size * 64;

		/* create data record array in segment record */
		seg_rcd->data_records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_ccgx_dmc_firmware_data_record_free);

		/* read row data in segment */
		row_buf = g_malloc0 (actual_row_size);
		for (int row = 0; row < seg_rcd->info_header.num_rows; row++) {
			g_autoptr(FuCcgxDmcFirmwareDataRecord) data_rcd = NULL;

			/* set row data offset */
			fw_buffer_offset = row_data_offset;

			/* read row data */
			if (!fu_memcpy_safe (row_buf, actual_row_size, 0x0,		/* dst */
					     fw_buf, fw_bufsz, fw_buffer_offset,	/* src */
					     actual_row_size, error))
				return FALSE;

			/* update hash */
			g_checksum_update (csum, (guchar *) row_buf, actual_row_size);

			/* add row data to data record */
			data_rcd = g_new0 (FuCcgxDmcFirmwareDataRecord, 1);
			data_rcd->data = g_bytes_new (row_buf, actual_row_size);

			/* add data record to data record array */
			g_ptr_array_add (seg_rcd->data_records, g_steal_pointer (&data_rcd));

			/* increment row data offset */
			row_data_offset += actual_row_size;
		}

		/* add segment record to segment array */
		g_ptr_array_add (img_rcd->seg_records, g_steal_pointer (&seg_rcd));

		/* increment segment info offset */
		*segment_info_offset += sizeof(FwctSegmentationInfo);
	}

	/* check checksum */
	g_checksum_get_digest (csum, hash, &digestlen);
	if (memcmp (hash, img_rcd->info_header.img_digest, DMC_HASH_SIZE) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid hash");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_dmc_firmware_parse_image (FuFirmware *firmware,
				  const guint8 *fw_buf,
				  gsize fw_bufsz,
				  GError **error)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE (firmware);
	gsize fw_buffer_offset = 0;
	gsize img_info_offset = sizeof(FwctInfo);
	gsize seg_info_offset = 0;

	/* set initial segment info offset */
	seg_info_offset = img_info_offset + self->fwct_info.image_count * sizeof(FwctImageInfo);
	for (guint32 img_num = 0; img_num < self->fwct_info.image_count; img_num++) {
		g_autoptr(FuCcgxDmcFirmwareImageRecord) img_rcd = NULL;

		/* set image info offset to fw buffer */
		fw_buffer_offset = img_info_offset;

		/* read image info */
		img_rcd = g_new0 (FuCcgxDmcFirmwareImageRecord, 1);
		if (!fu_memcpy_safe ((guint8 *) &img_rcd->info_header, sizeof(img_rcd->info_header), 0x0, /* dst */
				     fw_buf, fw_bufsz, fw_buffer_offset,	/* src */
				     sizeof(img_rcd->info_header), error))
			return FALSE;

		if (img_rcd->info_header.num_img_segments == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid segment number = %d",
				     img_rcd->info_header.num_img_segments);
			return FALSE;
		}
		if (img_rcd->info_header.row_size == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid row size = %d",
				     img_rcd->info_header.row_size);
			return FALSE;
		}

		/* parse segment */
		if (!fu_ccgx_dmc_firmware_parse_segment (firmware, fw_buf, fw_bufsz, img_rcd,
							 &seg_info_offset, error))
			return FALSE;

		/* add image record to image record array */
		g_ptr_array_add (self->image_records, g_steal_pointer (&img_rcd));

		/* increment image offset */
		img_info_offset += sizeof(FwctImageInfo);
	}

	return TRUE;
}

static gboolean
fu_ccgx_dmc_firmware_parse (FuFirmware *firmware,
			    GBytes *fw,
			    guint64 addr_start,
			    guint64 addr_end,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE (firmware);
	gsize fw_bufsz = 0;
	guint16 custom_meta_bufsz = 0;
	const guint8 *fw_buf = g_bytes_get_data (fw, &fw_bufsz);
	g_autofree guint8 *custom_meta_buf = NULL;
	g_autofree guint8 *fwct_buf = NULL;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* read fwct info */
	if (!fu_memcpy_safe ((guint8 *) &self->fwct_info, sizeof(self->fwct_info), 0x0, /* dst */
			     fw_buf, fw_bufsz, 0, /* src */
			     sizeof(self->fwct_info), error))
		return FALSE;

	/* check for 'F' 'W' 'C' 'T' in signature */
	if (self->fwct_info.signature != DMC_FWCT_SIGN) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid dmc signature, expected 0x%04X got 0x%04X",
			     (guint32) DMC_FWCT_SIGN,
			     (guint32) self->fwct_info.signature);
		return FALSE;
	}
	/* check fwct size */
	if (self->fwct_info.size > DMC_FWCT_MAX_SIZE ||
	    self->fwct_info.size == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid dmc fwct size, expected equal to or less than %u, got %u",
			     (guint32) DMC_FWCT_MAX_SIZE,
			     (guint32) self->fwct_info.size);
		return FALSE;
	}

	/* set version */
	if (self->fwct_info.composite_version != 0) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_from_uint32 (self->fwct_info.composite_version,
						     FWUPD_VERSION_FORMAT_QUAD);
		fu_firmware_set_version (firmware, ver);
		fu_firmware_set_version_raw (firmware, self->fwct_info.composite_version);
	}

	/* read fwct data */
	fwct_buf = g_malloc0 (self->fwct_info.size);
	if (!fu_memcpy_safe ((guint8 *) fwct_buf, self->fwct_info.size, 0x0,	/* dst */
			     fw_buf, fw_bufsz, 0,				/* src */
			     self->fwct_info.size, error))
		return FALSE;

	/* create fwct binary */
	self->fwct_blob = g_bytes_new (fwct_buf, self->fwct_info.size);

	/* create custom meta binary */
	if (!fu_common_read_uint16_safe (fw_buf, fw_bufsz, self->fwct_info.size, &custom_meta_bufsz,
			    G_LITTLE_ENDIAN,error))
		return FALSE;

	if (custom_meta_bufsz > 0) {
		/* alloc custom meta buffer */
		custom_meta_buf = g_malloc0 (custom_meta_bufsz);
		/* read custom metadata */
		if (!fu_memcpy_safe ((guint8 *)custom_meta_buf, custom_meta_bufsz, 0x0,	/* dst */
				     fw_buf, fw_bufsz, self->fwct_info.size + 2,	/* src */
				     custom_meta_bufsz, error))
			return FALSE;
		self->custom_meta_blob = g_bytes_new (custom_meta_buf, custom_meta_bufsz);
	}

	/* set row data start offset */
	self->row_data_offset_start = self->fwct_info.size + DMC_CUSTOM_META_LENGTH_FIELD_SIZE + custom_meta_bufsz;
	self->fw_data_size = fw_bufsz - self->row_data_offset_start;

	/* parse image */
	if (!fu_ccgx_dmc_firmware_parse_image (firmware, fw_buf, fw_bufsz, error))
		return FALSE;

	/* add something, although we'll use the records for the update */
	fu_firmware_image_set_addr (img, 0x0);
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_ccgx_dmc_firmware_init (FuCcgxDmcFirmware *self)
{
	self->image_records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_ccgx_dmc_firmware_image_record_free);
}

static void
fu_ccgx_dmc_firmware_finalize (GObject *object)
{
	FuCcgxDmcFirmware *self = FU_CCGX_DMC_FIRMWARE (object);

	if (self->fwct_blob != NULL)
		g_bytes_unref (self->fwct_blob);
	if (self->custom_meta_blob != NULL)
		g_bytes_unref (self->custom_meta_blob);
	if (self->image_records != NULL)
		g_ptr_array_unref (self->image_records);

	G_OBJECT_CLASS (fu_ccgx_dmc_firmware_parent_class)->finalize (object);
}

static void
fu_ccgx_dmc_firmware_class_init (FuCcgxDmcFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	object_class->finalize = fu_ccgx_dmc_firmware_finalize;
	klass_firmware->parse = fu_ccgx_dmc_firmware_parse;
	klass_firmware->to_string = fu_ccgx_dmc_firmware_to_string;
}

FuFirmware *
fu_ccgx_dmc_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_CCGX_DMC_FIRMWARE, NULL));
}
