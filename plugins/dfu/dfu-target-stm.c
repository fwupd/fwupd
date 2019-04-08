/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dfu-common.h"
#include "dfu-sector.h"
#include "dfu-target-stm.h"
#include "dfu-target-private.h"

#include "fwupd-error.h"

G_DEFINE_TYPE (DfuTargetStm, dfu_target_stm, DFU_TYPE_TARGET)

/* STMicroelectronics STM32 version of DFU:
 * www.st.com/resource/en/application_note/cd00264379.pdf */
#define DFU_STM_CMD_GET_COMMAND			0x00
#define DFU_STM_CMD_SET_ADDRESS_POINTER		0x21
#define DFU_STM_CMD_ERASE			0x41
#define DFU_STM_CMD_READ_UNPROTECT		0x92

static gboolean
dfu_target_stm_attach (DfuTarget *target, GError **error)
{
	g_autoptr(GBytes) bytes_tmp = g_bytes_new (NULL, 0);
	return dfu_target_download_chunk (target, 2, bytes_tmp, error);
}

static gboolean
dfu_target_stm_mass_erase (DfuTarget *target, GError **error)
{
	GBytes *data_in;
	guint8 buf[1];

	/* format buffer */
	buf[0] = DFU_STM_CMD_ERASE;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot mass-erase: ");
		return FALSE;
	}

	/* 2nd check required to get error code */
	return dfu_target_check_status (target, error);
}

/**
 * dfu_target_stm_set_address:
 * @target: a #DfuTarget
 * @address: memory address
 * @error: a #GError, or %NULL
 *
 * Sets the address used for the next download or upload request.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_stm_set_address (DfuTarget *target, guint32 address, GError **error)
{
	GBytes *data_in;
	guint8 buf[5];

	/* format buffer */
	buf[0] = DFU_STM_CMD_SET_ADDRESS_POINTER;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot set address 0x%x: ", address);
		return FALSE;
	}

	/* 2nd check required to get error code */
	g_debug ("doing actual check status");
	return dfu_target_check_status (target, error);
}

static DfuElement *
dfu_target_stm_upload_element (DfuTarget *target,
			       guint32 address,
			       gsize expected_size,
			       gsize maximum_size,
			       GError **error)
{
	DfuDevice *device = dfu_target_get_device (target);
	DfuSector *sector;
	DfuElement *element = NULL;
	GBytes *chunk_tmp;
	guint32 offset = address;
	guint percentage_size = expected_size > 0 ? expected_size : maximum_size;
	gsize total_size = 0;
	guint16 transfer_size = dfu_device_get_transfer_size (device);
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GBytes) contents_truncated = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* for DfuSe devices we need to handle the address manually */
	sector = dfu_target_get_sector_for_addr (target, offset);
	if (sector == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no memory sector at 0x%04x",
			     (guint) offset);
		return NULL;
	}
	g_debug ("using sector %u for read of %x",
		 dfu_sector_get_id (sector),
		 offset);
	if (!dfu_sector_has_cap (sector, DFU_SECTOR_CAP_READABLE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "memory sector at 0x%04x is not readable",
			     (guint) offset);
		return NULL;
	}

	/* update UI */
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_READ);

	/* manually set the sector address */
	g_debug ("setting DfuSe address to 0x%04x", (guint) offset);
	if (!dfu_target_stm_set_address (target, offset, error))
		return NULL;

	/* abort back to IDLE */
	if (!dfu_device_abort (device, error))
		return NULL;

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint16 idx = 0; idx < G_MAXUINT16; idx++) {
		guint32 chunk_size;

		/* read chunk of data -- ST uses wBlockNum=0 for DfuSe commands
		 * and wBlockNum=1 is reserved */
		chunk_tmp = dfu_target_upload_chunk (target,
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
			dfu_target_set_percentage (target, total_size, percentage_size);

		/* detect short write as EOF */
		if (chunk_size < transfer_size)
			break;

		/* more data than we needed */
		if (maximum_size > 0 && total_size > maximum_size)
			break;
	}

	/* abort back to IDLE */
	if (!dfu_device_abort (device, error))
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
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* create new image */
	contents = dfu_utils_bytes_join_array (chunks);
	if (expected_size > 0)
		contents_truncated = g_bytes_new_from_bytes (contents, 0, expected_size);
	else
		contents_truncated = g_bytes_ref (contents);
	element = dfu_element_new ();
	dfu_element_set_contents (element, contents_truncated);
	dfu_element_set_address (element, address);
	return element;
}

/**
 * dfu_target_stm_erase_address:
 * @target: a #DfuTarget
 * @address: memory address
 * @error: a #GError, or %NULL
 *
 * Erases a memory sector at a given address.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_stm_erase_address (DfuTarget *target, guint32 address, GError **error)
{
	GBytes *data_in;
	guint8 buf[5];

	/* format buffer */
	buf[0] = DFU_STM_CMD_ERASE;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, error)) {
		g_prefix_error (error, "cannot erase address 0x%x: ", address);
		return FALSE;
	}

	/* 2nd check required to get error code */
	g_debug ("doing actual check status");
	return dfu_target_check_status (target, error);
}

static gboolean
dfu_target_stm_download_element (DfuTarget *target,
				 DfuElement *element,
				 DfuTargetTransferFlags flags,
				 GError **error)
{
	DfuDevice *device = dfu_target_get_device (target);
	DfuSector *sector;
	GBytes *bytes;
	guint nr_chunks;
	guint zone_last = G_MAXUINT;
	guint16 transfer_size = dfu_device_get_transfer_size (device);
	g_autoptr(GPtrArray) sectors_array = NULL;
	g_autoptr(GHashTable) sectors_hash = NULL;

	/* round up as we have to transfer incomplete blocks */
	bytes = dfu_element_get_contents (element);
	nr_chunks = (guint) ceil ((gdouble) g_bytes_get_size (bytes) /
				  (gdouble) transfer_size);
	if (nr_chunks == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "zero-length firmware");
		return FALSE;
	}

	/* 1st pass: work out which sectors need erasing */
	sectors_array = g_ptr_array_new ();
	sectors_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (guint i = 0; i < nr_chunks; i++) {
		guint32 offset_dev;

		/* for DfuSe devices we need to handle the erase and setting
		 * the sectory address manually */
		offset_dev = dfu_element_get_address (element) + (i * transfer_size);
		sector = dfu_target_get_sector_for_addr (target, offset_dev);
		if (sector == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no memory sector at 0x%04x",
				     (guint) offset_dev);
			return FALSE;
		}
		if (!dfu_sector_has_cap (sector, DFU_SECTOR_CAP_WRITEABLE)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "memory sector at 0x%04x is not writable",
				     (guint) offset_dev);
			return FALSE;
		}

		/* if it's erasable and not yet blanked */
		if (dfu_sector_has_cap (sector, DFU_SECTOR_CAP_ERASEABLE) &&
		    g_hash_table_lookup (sectors_hash, sector) == NULL) {
			g_hash_table_insert (sectors_hash,
					     sector,
					     GINT_TO_POINTER (1));
			g_ptr_array_add (sectors_array, sector);
			g_debug ("marking sector 0x%04x-%04x to be erased",
				 dfu_sector_get_address (sector),
				 dfu_sector_get_address (sector) + dfu_sector_get_size (sector));
		}
	}

	/* 2nd pass: actually erase sectors */
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_ERASE);
	for (guint i = 0; i < sectors_array->len; i++) {
		sector = g_ptr_array_index (sectors_array, i);
		g_debug ("erasing sector at 0x%04x",
			 dfu_sector_get_address (sector));
		if (!dfu_target_stm_erase_address (target,
						   dfu_sector_get_address (sector),
						   error))
			return FALSE;
		dfu_target_set_percentage (target, i + 1, sectors_array->len);
	}
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* 3rd pass: write data */
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < nr_chunks; i++) {
		gsize length;
		guint32 offset;
		guint32 offset_dev;
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* caclulate the offset into the element data */
		offset = i * transfer_size;
		offset_dev = dfu_element_get_address (element) + offset;

		/* for DfuSe devices we need to set the address manually */
		sector = dfu_target_get_sector_for_addr (target, offset_dev);
		g_assert (sector != NULL);

		/* manually set the sector address */
		if (dfu_sector_get_zone (sector) != zone_last) {
			g_debug ("setting address to 0x%04x",
				 (guint) offset_dev);
			if (!dfu_target_stm_set_address (target,
							 (guint32) offset_dev,
							 error))
				return FALSE;
			zone_last = dfu_sector_get_zone (sector);
		}

		/* we have to write one final zero-sized chunk for EOF */
		length = g_bytes_get_size (bytes) - offset;
		if (length > transfer_size)
			length = transfer_size;
		bytes_tmp = g_bytes_new_from_bytes (bytes, offset, length);
		g_debug ("writing sector at 0x%04x (0x%" G_GSIZE_FORMAT ")",
			 offset_dev,
			 g_bytes_get_size (bytes_tmp));
		/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
		if (!dfu_target_download_chunk (target,
						(guint8) (i + 2),
						bytes_tmp,
						error))
			return FALSE;

		/* getting the status moves the state machine to DNLOAD-IDLE */
		if (!dfu_target_check_status (target, error))
			return FALSE;

		/* update UI */
		dfu_target_set_percentage (target, offset, g_bytes_get_size (bytes));
	}

	/* done */
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* success */
	return TRUE;
}

static void
dfu_target_stm_init (DfuTargetStm *target_stm)
{
}

static void
dfu_target_stm_class_init (DfuTargetStmClass *klass)
{
	DfuTargetClass *klass_target = DFU_TARGET_CLASS (klass);
	klass_target->attach = dfu_target_stm_attach;
	klass_target->mass_erase = dfu_target_stm_mass_erase;
	klass_target->upload_element = dfu_target_stm_upload_element;
	klass_target->download_element = dfu_target_stm_download_element;
}

DfuTarget *
dfu_target_stm_new (void)
{
	DfuTarget *target;
	target = g_object_new (DFU_TYPE_TARGET_STM, NULL);
	return target;
}
