/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-dfu-common.h"

/**
 * fu_dfu_state_to_string:
 * @state: a #FuDfuState, e.g. %FU_DFU_STATE_DFU_MANIFEST
 *
 * Converts an enumerated value to a string.
 *
 * Returns: a string
 **/
const gchar *
fu_dfu_state_to_string(FuDfuState state)
{
	if (state == FU_DFU_STATE_APP_IDLE)
		return "appIDLE";
	if (state == FU_DFU_STATE_APP_DETACH)
		return "appDETACH";
	if (state == FU_DFU_STATE_DFU_IDLE)
		return "dfuIDLE";
	if (state == FU_DFU_STATE_DFU_DNLOAD_SYNC)
		return "dfuDNLOAD-SYNC";
	if (state == FU_DFU_STATE_DFU_DNBUSY)
		return "dfuDNBUSY";
	if (state == FU_DFU_STATE_DFU_DNLOAD_IDLE)
		return "dfuDNLOAD-IDLE";
	if (state == FU_DFU_STATE_DFU_MANIFEST_SYNC)
		return "dfuMANIFEST-SYNC";
	if (state == FU_DFU_STATE_DFU_MANIFEST)
		return "dfuMANIFEST";
	if (state == FU_DFU_STATE_DFU_MANIFEST_WAIT_RESET)
		return "dfuMANIFEST-WAIT-RESET";
	if (state == FU_DFU_STATE_DFU_UPLOAD_IDLE)
		return "dfuUPLOAD-IDLE";
	if (state == FU_DFU_STATE_DFU_ERROR)
		return "dfuERROR";
	return NULL;
}

/**
 * fu_dfu_status_to_string:
 * @status: a #FuDfuStatus, e.g. %FU_DFU_STATUS_ERR_ERASE
 *
 * Converts an enumerated value to a string.
 *
 * Returns: a string
 **/
const gchar *
fu_dfu_status_to_string(FuDfuStatus status)
{
	if (status == FU_DFU_STATUS_OK)
		return "OK";
	if (status == FU_DFU_STATUS_ERR_TARGET)
		return "errTARGET";
	if (status == FU_DFU_STATUS_ERR_FILE)
		return "errFILE";
	if (status == FU_DFU_STATUS_ERR_WRITE)
		return "errwrite";
	if (status == FU_DFU_STATUS_ERR_ERASE)
		return "errERASE";
	if (status == FU_DFU_STATUS_ERR_CHECK_ERASED)
		return "errCHECK_ERASED";
	if (status == FU_DFU_STATUS_ERR_PROG)
		return "errPROG";
	if (status == FU_DFU_STATUS_ERR_VERIFY)
		return "errVERIFY";
	if (status == FU_DFU_STATUS_ERR_ADDRESS)
		return "errADDRESS";
	if (status == FU_DFU_STATUS_ERR_NOTDONE)
		return "errNOTDONE";
	if (status == FU_DFU_STATUS_ERR_FIRMWARE)
		return "errFIRMWARE";
	if (status == FU_DFU_STATUS_ERR_VENDOR)
		return "errVENDOR";
	if (status == FU_DFU_STATUS_ERR_USBR)
		return "errUSBR";
	if (status == FU_DFU_STATUS_ERR_POR)
		return "errPOR";
	if (status == FU_DFU_STATUS_ERR_UNKNOWN)
		return "errUNKNOWN";
	if (status == FU_DFU_STATUS_ERR_STALLDPKT)
		return "errSTALLDPKT";
	return NULL;
}

/**
 * fu_dfu_utils_bytes_join_array:
 * @chunks: (element-type GBytes): bytes
 *
 * Creates a monolithic block of memory from an array of #GBytes.
 *
 * Returns: (transfer full): a new GBytes
 **/
GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks)
{
	gsize total_size = 0;
	guint32 offset = 0;
	guint8 *buffer;

	/* get the size of all the chunks */
	for (guint i = 0; i < chunks->len; i++) {
		GBytes *chunk_tmp = g_ptr_array_index(chunks, i);
		total_size += g_bytes_get_size(chunk_tmp);
	}

	/* copy them into a buffer */
	buffer = g_malloc0(total_size);
	for (guint i = 0; i < chunks->len; i++) {
		const guint8 *chunk_data;
		gsize chunk_size = 0;
		GBytes *chunk_tmp = g_ptr_array_index(chunks, i);
		chunk_data = g_bytes_get_data(chunk_tmp, &chunk_size);
		if (chunk_size == 0)
			continue;
		memcpy(buffer + offset, chunk_data, chunk_size);
		offset += chunk_size;
	}
	return g_bytes_new_take(buffer, total_size);
}
