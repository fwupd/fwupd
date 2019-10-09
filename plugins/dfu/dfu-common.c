/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * SECTION:dfu-common
 * @short_description: Common functions for DFU
 *
 * These helper objects allow converting from enum values to strings.
 */

#include "config.h"

#include <string.h>

#include "dfu-common.h"

/**
 * dfu_state_to_string:
 * @state: a #DfuState, e.g. %DFU_STATE_DFU_MANIFEST
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 **/
const gchar *
dfu_state_to_string (DfuState state)
{
	if (state == DFU_STATE_APP_IDLE)
		return "appIDLE";
	if (state == DFU_STATE_APP_DETACH)
		return "appDETACH";
	if (state == DFU_STATE_DFU_IDLE)
		return "dfuIDLE";
	if (state == DFU_STATE_DFU_DNLOAD_SYNC)
		return "dfuDNLOAD-SYNC";
	if (state == DFU_STATE_DFU_DNBUSY)
		return "dfuDNBUSY";
	if (state == DFU_STATE_DFU_DNLOAD_IDLE)
		return "dfuDNLOAD-IDLE";
	if (state == DFU_STATE_DFU_MANIFEST_SYNC)
		return "dfuMANIFEST-SYNC";
	if (state == DFU_STATE_DFU_MANIFEST)
		return "dfuMANIFEST";
	if (state == DFU_STATE_DFU_MANIFEST_WAIT_RESET)
		return "dfuMANIFEST-WAIT-RESET";
	if (state == DFU_STATE_DFU_UPLOAD_IDLE)
		return "dfuUPLOAD-IDLE";
	if (state == DFU_STATE_DFU_ERROR)
		return "dfuERROR";
	return NULL;
}

/**
 * dfu_status_to_string:
 * @status: a #DfuStatus, e.g. %DFU_STATUS_ERR_ERASE
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 **/
const gchar *
dfu_status_to_string (DfuStatus status)
{
	if (status == DFU_STATUS_OK)
		return "OK";
	if (status == DFU_STATUS_ERR_TARGET)
		return "errTARGET";
	if (status == DFU_STATUS_ERR_FILE)
		return "errFILE";
	if (status == DFU_STATUS_ERR_WRITE)
		return "errwrite";
	if (status == DFU_STATUS_ERR_ERASE)
		return "errERASE";
	if (status == DFU_STATUS_ERR_CHECK_ERASED)
		return "errCHECK_ERASED";
	if (status == DFU_STATUS_ERR_PROG)
		return "errPROG";
	if (status == DFU_STATUS_ERR_VERIFY)
		return "errVERIFY";
	if (status == DFU_STATUS_ERR_ADDRESS)
		return "errADDRESS";
	if (status == DFU_STATUS_ERR_NOTDONE)
		return "errNOTDONE";
	if (status == DFU_STATUS_ERR_FIRMWARE)
		return "errFIRMWARE";
	if (status == DFU_STATUS_ERR_VENDOR)
		return "errVENDOR";
	if (status == DFU_STATUS_ERR_USBR)
		return "errUSBR";
	if (status == DFU_STATUS_ERR_POR)
		return "errPOR";
	if (status == DFU_STATUS_ERR_UNKNOWN)
		return "errUNKNOWN";
	if (status == DFU_STATUS_ERR_STALLDPKT)
		return "errSTALLDPKT";
	return NULL;
}

/**
 * dfu_version_to_string:
 * @version: a #DfuVersion, e.g. %DFU_VERSION_DFU_1_1
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 **/
const gchar *
dfu_version_to_string (DfuVersion version)
{
	if (version == DFU_VERSION_DFU_1_0)
		return "1.0";
	if (version == DFU_VERSION_DFU_1_1)
		return "1.1";
	if (version == DFU_VERSION_DFUSE)
		return "DfuSe";
	if (version == DFU_VERSION_ATMEL_AVR)
		return "AtmelAVR";
	return NULL;
}

/**
 * dfu_utils_bytes_join_array:
 * @chunks: (element-kind GBytes): bytes
 *
 * Creates a monolithic block of memory from an array of #GBytes.
 *
 * Return value: (transfer full): a new GBytes
 **/
GBytes *
dfu_utils_bytes_join_array (GPtrArray *chunks)
{
	gsize total_size = 0;
	guint32 offset = 0;
	guint8 *buffer;

	/* get the size of all the chunks */
	for (guint i = 0; i < chunks->len; i++) {
		GBytes *chunk_tmp = g_ptr_array_index (chunks, i);
		total_size += g_bytes_get_size (chunk_tmp);
	}

	/* copy them into a buffer */
	buffer = g_malloc0 (total_size);
	for (guint i = 0; i < chunks->len; i++) {
		const guint8 *chunk_data;
		gsize chunk_size = 0;
		GBytes *chunk_tmp = g_ptr_array_index (chunks, i);
		chunk_data = g_bytes_get_data (chunk_tmp, &chunk_size);
		if (chunk_size == 0)
			continue;
		memcpy (buffer + offset, chunk_data, chunk_size);
		offset += chunk_size;
	}
	return g_bytes_new_take (buffer, total_size);
}
