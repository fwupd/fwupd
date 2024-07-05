/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fu-dfu-common.h"
#include "fu-dfu-sector.h"
#include "fu-dfu-target-private.h" /* waive-pre-commit */
#include "fu-dfu-target-stm.h"

G_DEFINE_TYPE(FuDfuTargetStm, fu_dfu_target_stm, FU_TYPE_DFU_TARGET)

/* STMicroelectronics STM32 version of DFU:
 * www.st.com/resource/en/application_note/cd00264379.pdf */
#define DFU_STM_CMD_GET_COMMAND		0x00
#define DFU_STM_CMD_SET_ADDRESS_POINTER 0x21
#define DFU_STM_CMD_ERASE		0x41
#define DFU_STM_CMD_READ_UNPROTECT	0x92

static gboolean
fu_dfu_target_stm_attach(FuDfuTarget *target, FuProgress *progress, GError **error)
{
	/* downloading empty payload will cause a dfu to leave,
	 * the returned status will be dfuMANIFEST and expect the device to disconnect */
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;
	if (!fu_dfu_target_download_chunk(target, 2, buf, 0, progress, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to attach: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dfu_target_stm_mass_erase(FuDfuTarget *target, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* format buffer */
	fu_byte_array_append_uint8(buf, DFU_STM_CMD_ERASE);
	if (!fu_dfu_target_download_chunk(target, 0, buf, 35000, progress, error)) {
		g_prefix_error(error, "cannot mass-erase: ");
		return FALSE;
	}

	/* 2nd check required to get error code */
	return fu_dfu_target_check_status(target, error);
}

/* sets the address used for the next download or upload request */
static gboolean
fu_dfu_target_stm_set_address(FuDfuTarget *target,
			      guint32 address,
			      FuProgress *progress,
			      GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* format buffer */
	fu_byte_array_append_uint8(buf, DFU_STM_CMD_SET_ADDRESS_POINTER);
	fu_byte_array_append_uint32(buf, address, G_LITTLE_ENDIAN);
	if (!fu_dfu_target_download_chunk(target, 0, buf, 0, progress, error)) {
		g_prefix_error(error, "cannot set address 0x%x: ", address);
		return FALSE;
	}

	/* 2nd check required to get error code */
	g_debug("doing actual check status");
	return fu_dfu_target_check_status(target, error);
}

static FuChunk *
fu_dfu_target_stm_upload_element(FuDfuTarget *target,
				 guint32 address,
				 gsize expected_size,
				 gsize maximum_size,
				 FuProgress *progress,
				 GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(target)));
	FuDfuSector *sector;
	FuChunk *chk = NULL;
	GBytes *chunk_tmp;
	guint32 offset = address;
	guint percentage_size = expected_size > 0 ? expected_size : maximum_size;
	gsize total_size = 0;
	guint16 transfer_size = fu_dfu_device_get_transfer_size(device);
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GBytes) contents_truncated = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 40, "set-addr");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "abort");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 58, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	/* for DfuSe devices we need to handle the address manually */
	sector = fu_dfu_target_get_sector_for_addr(target, offset);
	if (sector == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no memory sector at 0x%04x",
			    (guint)offset);
		return NULL;
	}
	g_debug("using sector %u for read of %x", fu_dfu_sector_get_id(sector), offset);
	if (!fu_dfu_sector_has_cap(sector, FU_DFU_SECTOR_CAP_READABLE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "memory sector at 0x%04x is not readable",
			    (guint)offset);
		return NULL;
	}

	/* manually set the sector address */
	g_debug("setting DfuSe address to 0x%04x", (guint)offset);
	if (!fu_dfu_target_stm_set_address(target, offset, fu_progress_get_child(progress), error))
		return NULL;
	fu_progress_step_done(progress);

	/* abort back to IDLE */
	if (!fu_dfu_device_abort(device, error))
		return NULL;
	fu_progress_step_done(progress);

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	for (guint16 idx = 0; idx < G_MAXUINT16; idx++) {
		guint32 chunk_size;
		g_autoptr(FuProgress) progress_tmp = fu_progress_new(G_STRLOC);

		/* read chunk of data -- ST uses wBlockNum=0 for DfuSe commands
		 * and wBlockNum=1 is reserved */
		chunk_tmp = fu_dfu_target_upload_chunk(target,
						       idx + 2,
						       0,	     /* device transfer size */
						       progress_tmp, /* we don't know! */
						       error);
		if (chunk_tmp == NULL)
			return NULL;

		/* add to array */
		chunk_size = (guint32)g_bytes_get_size(chunk_tmp);
		g_debug("got #%04x chunk @0x%x of size %" G_GUINT32_FORMAT,
			idx,
			offset,
			chunk_size);
		g_ptr_array_add(chunks, chunk_tmp);
		total_size += chunk_size;
		offset += chunk_size;

		/* update UI */
		if (chunk_size > 0) {
			fu_progress_set_percentage_full(fu_progress_get_child(progress),
							MIN(total_size, percentage_size),
							percentage_size);
		}

		/* detect short write as EOF */
		if (chunk_size < transfer_size)
			break;

		/* more data than we needed */
		if (maximum_size > 0 && total_size > maximum_size)
			break;
	}
	fu_progress_step_done(progress);

	/* abort back to IDLE */
	if (!fu_dfu_device_abort(device, error))
		return NULL;
	fu_progress_step_done(progress);

	/* check final size */
	if (expected_size > 0) {
		if (total_size < expected_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid size, got %" G_GSIZE_FORMAT ", "
				    "expected %" G_GSIZE_FORMAT,
				    total_size,
				    expected_size);
			return NULL;
		}
	}

	/* create new image */
	contents = fu_dfu_utils_bytes_join_array(chunks);
	if (expected_size > 0) {
		contents_truncated = fu_bytes_new_offset(contents, 0, expected_size, error);
		if (contents_truncated == NULL)
			return NULL;
	} else {
		contents_truncated = g_bytes_ref(contents);
	}
	chk = fu_chunk_bytes_new(contents_truncated);
	fu_chunk_set_address(chk, address);
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
fu_dfu_target_stm_erase_address(FuDfuTarget *target,
				guint32 address,
				FuProgress *progress,
				GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* format buffer */
	fu_byte_array_append_uint8(buf, DFU_STM_CMD_ERASE);
	fu_byte_array_append_uint32(buf, address, G_LITTLE_ENDIAN);
	if (!fu_dfu_target_download_chunk(target, 0, buf, 0, progress, error)) {
		g_prefix_error(error, "cannot erase address 0x%x: ", address);
		return FALSE;
	}

	/* 2nd check required to get error code */
	g_debug("doing actual check status");
	return fu_dfu_target_check_status(target, error);
}

static gboolean
fu_dfu_target_stm_download_element1(FuDfuTarget *target,
				    FuChunkArray *chunks,
				    GPtrArray *sectors_array,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(GHashTable) sectors_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
	gsize address = 0;
	gsize transfer_size = 0;

	/* start offset */
	if (fu_chunk_array_length(chunks) > 0) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, 0, error);
		if (chk == NULL)
			return FALSE;
		address = fu_chunk_get_address(chk);
		transfer_size = fu_chunk_get_data_sz(chk);
	}

	/* no progress */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint32 offset_dev = i * transfer_size;

		/* for DfuSe devices we need to handle the erase and setting
		 * the sector address manually */
		while (offset_dev < (i + 1) * transfer_size) {
			FuDfuSector *sector =
			    fu_dfu_target_get_sector_for_addr(target, address + offset_dev);
			if (sector == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no memory sector at 0x%04x",
					    (guint)address + offset_dev);
				return FALSE;
			}
			if (!fu_dfu_sector_has_cap(sector, FU_DFU_SECTOR_CAP_WRITEABLE)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "memory sector at 0x%04x is not writable",
					    (guint)address + offset_dev);
				return FALSE;
			}

			/* if it's erasable and not yet blanked */
			if (fu_dfu_sector_has_cap(sector, FU_DFU_SECTOR_CAP_ERASABLE) &&
			    g_hash_table_lookup(sectors_hash, sector) == NULL) {
				g_hash_table_insert(sectors_hash, sector, GINT_TO_POINTER(1));
				g_ptr_array_add(sectors_array, sector);
				g_debug("marking sector 0x%04x-%04x to be erased",
					fu_dfu_sector_get_address(sector),
					fu_dfu_sector_get_address(sector) +
					    fu_dfu_sector_get_size(sector));
			}
			offset_dev += fu_dfu_sector_get_size(sector);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_target_stm_download_element2(FuDfuTarget *target,
				    GPtrArray *sectors_array,
				    FuProgress *progress,
				    GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sectors_array->len);

	for (guint i = 0; i < sectors_array->len; i++) {
		FuDfuSector *sector = g_ptr_array_index(sectors_array, i);
		g_debug("erasing sector at 0x%04x", fu_dfu_sector_get_address(sector));
		if (!fu_dfu_target_stm_erase_address(target,
						     fu_dfu_sector_get_address(sector),
						     fu_progress_get_child(progress),
						     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_target_stm_download_element3(FuDfuTarget *target,
				    FuChunkArray *chunks,
				    GPtrArray *sectors_array,
				    FuProgress *progress,
				    GError **error)
{
	guint zone_last = G_MAXUINT;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		FuDfuSector *sector;
		g_autoptr(FuChunk) chk_tmp = NULL;
		g_autoptr(GByteArray) buf = g_byte_array_new();
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* prepare chunk */
		chk_tmp = fu_chunk_array_index(chunks, i, error);
		if (chk_tmp == NULL)
			return FALSE;

		/* for DfuSe devices we need to set the address manually */
		sector = fu_dfu_target_get_sector_for_addr(target, fu_chunk_get_address(chk_tmp));
		if (sector == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no valid sector for 0x%x",
				    (guint)fu_chunk_get_address(chk_tmp));
			return FALSE;
		}

		/* manually set the sector address */
		if (fu_dfu_sector_get_zone(sector) != zone_last) {
			g_autoptr(FuProgress) progress_tmp = fu_progress_new(G_STRLOC);
			g_debug("setting address to 0x%04x", (guint)fu_chunk_get_address(chk_tmp));
			if (!fu_dfu_target_stm_set_address(target,
							   (guint32)fu_chunk_get_address(chk_tmp),
							   progress_tmp,
							   error))
				return FALSE;
			zone_last = fu_dfu_sector_get_zone(sector);
		}

		/* we have to write one final zero-sized chunk for EOF */
		bytes_tmp = fu_chunk_get_bytes(chk_tmp);
		g_debug("writing sector at 0x%04x (0x%" G_GSIZE_FORMAT ")",
			(guint)fu_chunk_get_address(chk_tmp),
			g_bytes_get_size(bytes_tmp));

		/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
		fu_byte_array_append_bytes(buf, bytes_tmp);
		if (!fu_dfu_target_download_chunk(target,
						  (i + 2),
						  buf,
						  0, /* timeout default */
						  fu_progress_get_child(progress),
						  error)) {
			g_prefix_error(error, "failed to write STM chunk %u: ", i);
			return FALSE;
		}

		/* getting the status moves the state machine to DNLOAD-IDLE */
		if (!fu_dfu_target_check_status(target, error))
			return FALSE;

		/* update UI */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_target_stm_download_element(FuDfuTarget *target,
				   FuChunk *chk,
				   FuProgress *progress,
				   FuDfuTargetTransferFlags flags,
				   GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(target)));
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GPtrArray) sectors_array = g_ptr_array_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 49, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);

	/* 1st pass: work out which sectors need erasing */
	bytes = fu_chunk_get_bytes(chk);
	chunks = fu_chunk_array_new_from_bytes(bytes,
					       fu_chunk_get_address(chk),
					       fu_dfu_device_get_transfer_size(device));
	if (!fu_dfu_target_stm_download_element1(target,
						 chunks,
						 sectors_array,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* 2nd pass: actually erase sectors */
	if (!fu_dfu_target_stm_download_element2(target,
						 sectors_array,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* 3rd pass: write data */
	if (!fu_dfu_target_stm_download_element3(target,
						 chunks,
						 sectors_array,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_dfu_target_stm_init(FuDfuTargetStm *self)
{
}

static void
fu_dfu_target_stm_class_init(FuDfuTargetStmClass *klass)
{
	FuDfuTargetClass *target_class = FU_DFU_TARGET_CLASS(klass);
	target_class->attach = fu_dfu_target_stm_attach;
	target_class->mass_erase = fu_dfu_target_stm_mass_erase;
	target_class->upload_element = fu_dfu_target_stm_upload_element;
	target_class->download_element = fu_dfu_target_stm_download_element;
}

FuDfuTarget *
fu_dfu_target_stm_new(void)
{
	FuDfuTarget *target;
	target = g_object_new(FU_TYPE_DFU_TARGET_STM, NULL);
	return target;
}
