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
 * dfu_cipher_kind_to_string:
 * @cipher_kind: a #DfuCipherKind, e.g. %DFU_CIPHER_KIND_XTEA
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 **/
const gchar *
dfu_cipher_kind_to_string (DfuCipherKind cipher_kind)
{
	if (cipher_kind == DFU_CIPHER_KIND_NONE)
		return "none";
	if (cipher_kind == DFU_CIPHER_KIND_XTEA)
		return "xtea";
	if (cipher_kind == DFU_CIPHER_KIND_RSA)
		return "rsa";
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

/**
 * dfu_utils_bytes_pad:
 * @bytes: a #GBytes
 * @sz: the desired size in bytes
 *
 * Pads a GBytes to a given @sz with `0xff`.
 *
 * Return value: (transfer full): a #GBytes
 **/
GBytes *
dfu_utils_bytes_pad (GBytes *bytes, gsize sz)
{
	gsize bytes_sz;

	g_return_val_if_fail (g_bytes_get_size (bytes) <= sz, NULL);

	/* pad */
	bytes_sz = g_bytes_get_size (bytes);
	if (bytes_sz < sz) {
		const guint8 *data = g_bytes_get_data (bytes, NULL);
		guint8 *data_new = g_malloc (sz);
		memcpy (data_new, data, bytes_sz);
		memset (data_new + bytes_sz, 0xff, sz - bytes_sz);
		return g_bytes_new_take (data_new, sz);
	}

	/* exactly right */
	return g_bytes_ref (bytes);
}

/**
 * dfu_utils_buffer_parse_uint4:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 1 byte long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 **/
guint8
dfu_utils_buffer_parse_uint4 (const gchar *data)
{
	gchar buffer[2];
	memcpy (buffer, data, 1);
	buffer[1] = '\0';
	return (guint8) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * dfu_utils_buffer_parse_uint8:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 2 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 **/
guint8
dfu_utils_buffer_parse_uint8 (const gchar *data)
{
	gchar buffer[3];
	memcpy (buffer, data, 2);
	buffer[2] = '\0';
	return (guint8) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * dfu_utils_buffer_parse_uint16:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 4 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 **/
guint16
dfu_utils_buffer_parse_uint16 (const gchar *data)
{
	gchar buffer[5];
	memcpy (buffer, data, 4);
	buffer[4] = '\0';
	return (guint16) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * dfu_utils_buffer_parse_uint24:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 6 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 **/
guint32
dfu_utils_buffer_parse_uint24 (const gchar *data)
{
	gchar buffer[7];
	memcpy (buffer, data, 6);
	buffer[6] = '\0';
	return (guint32) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * dfu_utils_buffer_parse_uint32:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 8 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 **/
guint32
dfu_utils_buffer_parse_uint32 (const gchar *data)
{
	gchar buffer[9];
	memcpy (buffer, data, 8);
	buffer[8] = '\0';
	return (guint32) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * dfu_utils_strnsplit:
 * @str: a string to split
 * @sz: size of @str
 * @delimiter: a string which specifies the places at which to split the string
 * @max_tokens: the maximum number of pieces to split @str into
 *
 * Splits a string into a maximum of @max_tokens pieces, using the given
 * delimiter. If @max_tokens is reached, the remainder of string is appended
 * to the last token.
 *
 * Return value: a newly-allocated NULL-terminated array of strings
 **/
gchar **
dfu_utils_strnsplit (const gchar *str, gsize sz,
		     const gchar *delimiter, gint max_tokens)
{
	if (str[sz - 1] != '\0') {
		g_autofree gchar *str2 = g_strndup (str, sz);
		return g_strsplit (str2, delimiter, max_tokens);
	}
	return g_strsplit (str, delimiter, max_tokens);
}
