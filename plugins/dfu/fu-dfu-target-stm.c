/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fu-dfu-common.h"
#include "fu-dfu-sector.h"
#include "fu-dfu-target-stm.h"
#include "fu-dfu-target-private.h" /* waive-pre-commit */

G_DEFINE_TYPE (FuDfuTargetStm, fu_dfu_target_stm, FU_TYPE_DFU_TARGET)

/* STMicroelectronics STM32 version of DFU:
 * www.st.com/resource/en/application_note/cd00264379.pdf */
#define DFU_STM_CMD_GET_COMMAND			0x00
#define DFU_STM_CMD_SET_ADDRESS_POINTER		0x21
#define DFU_STM_CMD_ERASE			0x41
#define DFU_STM_CMD_READ_UNPROTECT		0x92

static gboolean
fu_dfu_target_stm_attach (FuDfuTarget *target, GError **error)
{
	/* downloading empty payload will cause a dfu to leave,
	 * the returned status will be dfuMANIFEST and expect the device to disconnect */
	g_autoptr(GBytes) bytes_tmp = g_bytes_new (NULL, 0);
	return fu_dfu_target_download_chunk (target, 2, bytes_tmp, error);
}

static gboolean
fu_dfu_target_stm_mass_erase (FuDfuTarget *target, GError **error)
{
	GBytes *data_in;
	guint8 buf[1];

	/* format buffer */
	buf[0] = DFU_STM_CMD_ERASE;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!fu_dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot mass-erase: ");
		return FALSE;
	}

	/* 2nd check required to get error code */
	return fu_dfu_target_check_status (target, error);
}

/**
 * fu_dfu_target_stm_set_address:
 * @target: a #FuDfuTarget
 * @address: memory address
 * @error: (nullable): optional return location for an error
 *
 * Sets the address used for the next download or upload request.
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dfu_target_stm_set_address (FuDfuTarget *target, guint32 address, GError **error)
{
	GBytes *data_in;
	guint8 buf[5];

	/* format buffer */
	buf[0] = DFU_STM_CMD_SET_ADDRESS_POINTER;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!fu_dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot set address 0x%x: ", address);
		return FALSE;
	}

	/* 2nd check required to get error code */
	g_debug ("doing actual check status");
	return fu_dfu_target_check_status (target, error);
}

static FuChunk *
fu_dfu_target_stm_upload_element(FuDfuTarget *target,
				 guint32 address,
				 gsize expected_size,
				 gsize maximum_size,
				 FuProgress *progress,
				 GError **error)
{
	FuDfuDevice *device = fu_dfu_target_get_device (target);
	FuDfuSector *sector;
	FuChunk *chk = NULL;
	GBytes *chunk_tmp;
	guint32 offset = address;
	guint percentage_size = expected_size > 0 ? expected_size : maximum_size;
	gsize total_size = 0;
	guint16 transfer_size = fu_dfu_device_get_transfer_size (device);
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GBytes) contents_truncated = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* for DfuSe devices we need to handle the address manually */
	sector = fu_dfu_target_get_sector_for_addr (target, offset);
	if (sector == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no memory sector at 0x%04x",
			     (guint) offset);
		return NULL;
	}
	g_debug ("using sector %u for read of %x",
		 fu_dfu_sector_get_id (sector),
		 offset);
	if (!fu_dfu_sector_has_cap (sector, DFU_SECTOR_CAP_READABLE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "memory sector at 0x%04x is not readable",
			     (guint) offset);
		return NULL;
	}

	/* update UI */
	fu_dfu_target_set_action (target, FWUPD_STATUS_DEVICE_READ);

	/* manually set the sector address */
	g_debug ("setting DfuSe address to 0x%04x", (guint) offset);
	if (!fu_dfu_target_stm_set_address (target, offset, error))
		return NULL;

	/* abort back to IDLE */
	if (!fu_dfu_device_abort (device, error))
		return NULL;

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint16 idx = 0; idx < G_MAXUINT16; idx++) {
		guint32 chunk_size;

		/* read chunk of data -- ST uses wBlockNum=0 for DfuSe commands
		 * and wBlockNum=1 is reserved */
		chunk_tmp = fu_dfu_target_upload_chunk (target,
							idx + 2,
							0, /* device transfer size */
							error);
		if (chunk_tmp == NULL)
			return NULL;

		/* add to array */
		chunk_size = (guint32) g_bytes_get_size (chunk_tmp);
		g_debug ("got #%04x chunk @0x%x of size %" G_GUINT32_FORMAT,
			 idx, offset, chunk_size);
		g_ptr_array_add (chunks, chunk_tmp);
		total_size += chunk_size;
		offset += chunk_size;

		/* update UI */
		if (chunk_size > 0)
			fu_progress_set_percentage_full(progress, total_size, percentage_size);

		/* detect short write as EOF */
		if (chunk_size < transfer_size)
			break;

		/* more data than we needed */
		if (maximum_size > 0 && total_size > maximum_size)
			break;
	}

	/* abort back to IDLE */
	if (!fu_dfu_device_abort (device, error))
		return NULL;

	/* check final size */
	if (expected_size > 0) {
		if (total_size < expected_size) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid size, got %" G_GSIZE_FORMAT ", "
				     "expected %" G_GSIZE_FORMAT ,
				     total_size, expected_size);
			return NULL;
		}
	}

	/* done */
	fu_dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* create new image */
	contents = fu_dfu_utils_bytes_join_array (chunks);
	if (expected_size > 0) {
		contents_truncated = fu_common_bytes_new_offset (contents,
								 0,
								 expected_size,
								 error);
		if (contents_truncated == NULL)
			return NULL;
	} else {
		contents_truncated = g_bytes_ref (contents);
	}
	chk = fu_chunk_bytes_new (contents_truncated);
	fu_chunk_set_address (chk, address);
	return chk;
}

/**
 * fu_dfu_target_stm_erase_address:
 * @target: a #FuDfuTarget
 * @address: memory address
 * @error: (nullable): optional return location for an error
 *
 * Erases a memory sector at a given address.
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dfu_target_stm_erase_address (FuDfuTarget *target, guint32 address, GError **error)
{
	GBytes *data_in;
	guint8 buf[5];

	/* format buffer */
	buf[0] = DFU_STM_CMD_ERASE;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!fu_dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot erase address 0x%x: ", address);
		return FALSE;
	}

	/* 2nd check required to get error code */
	g_debug ("doing actual check status");
	return fu_dfu_target_check_status (target, error);
}

static gboolean
fu_dfu_target_stm_download_element(FuDfuTarget *target,
				   FuChunk *chk,
				   FuProgress *progress,
				   FuDfuTargetTransferFlags flags,
				   GError **error)
{
	FuDfuDevice *device = fu_dfu_target_get_device (target);
	FuDfuSector *sector;
	FuProgress *progress_local;
	guint nr_chunks;
	guint zone_last = G_MAXUINT;
	guint16 transfer_size = fu_dfu_device_get_transfer_size (device);
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GPtrArray) sectors_array = NULL;
	g_autoptr(GHashTable) sectors_hash = NULL;

	/* round up as we have to transfer incomplete blocks */
	bytes = fu_chunk_get_bytes (chk);
	nr_chunks = (guint) ceil ((gdouble) g_bytes_get_size (bytes) /
				  (gdouble) transfer_size);
	if (nr_chunks == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "zero-length firmware");
		return FALSE;
	}

	/* progress */
	fu_progress_set_custom_steps(progress,
				     1 /* 1st pass */,
				     10 /* 2nd pass */,
				     89 /* 3rd pass */,
				     -1);

	/* 1st pass: work out which sectors need erasing */
	sectors_array = g_ptr_array_new ();
	sectors_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (guint i = 0; i < nr_chunks; i++) {
		guint32 offset_dev;

		/* for DfuSe devices we need to handle the erase and setting
		 * the sectory address manually */
		offset_dev = i * transfer_size;
		while (offset_dev < (i + 1) * transfer_size) {
			sector = fu_dfu_target_get_sector_for_addr (target, fu_chunk_get_address (chk) + offset_dev);
			if (sector == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "no memory sector at 0x%04x",
					     (guint) fu_chunk_get_address (chk) + offset_dev);
				return FALSE;
			}
			if (!fu_dfu_sector_has_cap (sector, DFU_SECTOR_CAP_WRITEABLE)) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "memory sector at 0x%04x is not writable",
					     (guint) fu_chunk_get_address (chk) + offset_dev);
				return FALSE;
			}

			/* if it's erasable and not yet blanked */
			if (fu_dfu_sector_has_cap (sector, DFU_SECTOR_CAP_ERASEABLE) &&
			    g_hash_table_lookup (sectors_hash, sector) == NULL) {
				g_hash_table_insert (sectors_hash,
						     sector,
						     GINT_TO_POINTER (1));
				g_ptr_array_add (sectors_array, sector);
				g_debug ("marking sector 0x%04x-%04x to be erased",
					 fu_dfu_sector_get_address (sector),
					 fu_dfu_sector_get_address (sector) + fu_dfu_sector_get_size (sector));
			}
			offset_dev += fu_dfu_sector_get_size (sector);
		}
	}
	fu_progress_step_done(progress);

	/* 2nd pass: actually erase sectors */
	fu_dfu_target_set_action (target, FWUPD_STATUS_DEVICE_ERASE);
	progress_local = fu_progress_get_division(progress);
	for (guint i = 0; i < sectors_array->len; i++) {
		sector = g_ptr_array_index (sectors_array, i);
		g_debug ("erasing sector at 0x%04x",
			 fu_dfu_sector_get_address (sector));
		if (!fu_dfu_target_stm_erase_address (target,
						      fu_dfu_sector_get_address (sector),
						      error))
			return FALSE;
		fu_progress_set_percentage_full(progress_local, i + 1, sectors_array->len);
	}
	fu_progress_step_done(progress);

	/* 3rd pass: write data */
	fu_dfu_target_set_action (target, FWUPD_STATUS_DEVICE_WRITE);
	progress_local = fu_progress_get_division(progress);
	for (guint i = 0; i < nr_chunks; i++) {
		gsize length;
		guint32 offset;
		guint32 offset_dev;
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* caclulate the offset into the element data */
		offset = i * transfer_size;
		offset_dev = fu_chunk_get_address (chk) + offset;

		/* for DfuSe devices we need to set the address manually */
		sector = fu_dfu_target_get_sector_for_addr (target, offset_dev);
		g_assert (sector != NULL);

		/* manually set the sector address */
		if (fu_dfu_sector_get_zone (sector) != zone_last) {
			g_debug ("setting address to 0x%04x",
				 (guint) offset_dev);
			if (!fu_dfu_target_stm_set_address (target,
							    (guint32) offset_dev,
							    error))
				return FALSE;
			zone_last = fu_dfu_sector_get_zone (sector);
		}

		/* we have to write one final zero-sized chunk for EOF */
		length = g_bytes_get_size (bytes) - offset;
		if (length > transfer_size)
			length = transfer_size;
		bytes_tmp = fu_common_bytes_new_offset (bytes,
							offset,
							length,
							error);
		if (bytes_tmp == NULL)
			return FALSE;
		g_debug ("writing sector at 0x%04x (0x%" G_GSIZE_FORMAT ")",
			 offset_dev,
			 g_bytes_get_size (bytes_tmp));
		/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
		if (!fu_dfu_target_download_chunk (target,
						   (i + 2),
						   bytes_tmp,
						   error))
			return FALSE;

		/* getting the status moves the state machine to DNLOAD-IDLE */
		if (!fu_dfu_target_check_status (target, error))
			return FALSE;

		/* update UI */
		fu_progress_set_percentage_full(progress_local, offset, g_bytes_get_size(bytes));
	}

	/* done */
	fu_progress_step_done(progress);
	fu_dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* success */
	return TRUE;
}

static void
fu_dfu_target_stm_init (FuDfuTargetStm *self)
{
}

static void
fu_dfu_target_stm_class_init (FuDfuTargetStmClass *klass)
{
	FuDfuTargetClass *klass_target = FU_DFU_TARGET_CLASS (klass);
	klass_target->attach = fu_dfu_target_stm_attach;
	klass_target->mass_erase = fu_dfu_target_stm_mass_erase;
	klass_target->upload_element = fu_dfu_target_stm_upload_element;
	klass_target->download_element = fu_dfu_target_stm_download_element;
}

FuDfuTarget *
fu_dfu_target_stm_new (void)
{
	FuDfuTarget *target;
	target = g_object_new (FU_TYPE_DFU_TARGET_STM, NULL);
	return target;
}
