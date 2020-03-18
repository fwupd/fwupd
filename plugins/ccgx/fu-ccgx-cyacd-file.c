/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include "fu-ccgx-cyacd-file.h"

static guint8
ch_to_hex (guint8 value)
{
	if ('0' <= value && value <= '9') {
		return (guint8)(value - '0');
	}

	if ('a' <= value && value <= 'f') {
		return (guint8)(10 + value - 'a');
	}

	if ('A' <= value && value <= 'F') {
		return (guint8)(10 + value - 'A');
	}
	return 0;
}

static void
convert_ascii_to_hex (guint8 *src_buffer, guint32 src_size, guint8 *dst_buffer, guint32 dst_size)
{
	guint32 idx;
	guint32 src_index = 0;

	g_return_if_fail (src_buffer != NULL && src_size > 0);
	g_return_if_fail (dst_buffer != NULL && dst_size > 0);

	for (idx = 0; idx < dst_size; idx++) {
		src_index = 2 * idx;
		if (src_index + 1 >= src_size) {
			break;
		}
		dst_buffer[idx] = (ch_to_hex (src_buffer [src_index]) << 4) | ch_to_hex (src_buffer [src_index + 1]);
	}
}

static gboolean
cyacd_buffer_trim_cr_lf (CyacdFileHandle *handle)
{
	guint8 ch;

	g_return_val_if_fail (handle != NULL, FALSE);

	while (handle->pos < handle->buffer_size) {
		ch = handle->buffer [handle->pos];
		if (ch != '\n' &&  ch != '\r') {
			return TRUE;
		}
		handle->pos++;
	}
	return	FALSE;
}

static guint32
cyacd_buffer_read_line (CyacdFileHandle* handle, guint8 *line_buffer, guint32 line_buff_size)
{
	guint32 index = 0;
	guint8 ch;

	cyacd_buffer_trim_cr_lf (handle);

	while (handle->pos < handle->buffer_size && index < line_buff_size) {
		ch = handle->buffer [handle->pos];
		handle->pos++;

		if (ch == '\n' || ch == '\r') {
			break;
		}

		line_buffer [index] = ch;
		index++;
	}

	cyacd_buffer_trim_cr_lf (handle);
	return index;
}

/**
 * fu_ccgx_cyacd_file_init_handle:
 * @handle_array[in,out] Cyacd File Handle Array
 * @num_of_array number of Cyand File Handle Arrray
 * @buffer cyacd data buffer
 * @buffer_size cyacd data buffer size
 *
 * Initialize and setup up cyacd handle with user buffer
 *
 * Returns: number of valid handle
*/

guint32
fu_ccgx_cyacd_file_init_handle (CyacdFileHandle *handle_array, guint32 num_of_array, guint8 *buffer, guint32 buffer_size)
{
	CyacdFileHandle *current_handle = NULL;
	guint32 next_handle_index = 0;
	guint32 buffer_pos = 0;
	guint32 line_pos = 0;
	guint8 ch = 0;
	gboolean is_cr_lf = FALSE;

	g_return_val_if_fail (handle_array != NULL && num_of_array > 0, 0);
	g_return_val_if_fail (buffer != NULL && buffer_size > 0, 0);

	while (buffer_pos < buffer_size) {
		ch = buffer[buffer_pos];
		if (is_cr_lf == TRUE && ch != '\n' && ch != '\r') {
			line_pos = 0;
		}

		if (line_pos == 0) {
			if (ch != ':') {
				if (next_handle_index < num_of_array) {
					current_handle = &handle_array [next_handle_index];
					memset (current_handle, 0, sizeof(CyacdFileHandle));
					current_handle->buffer = &buffer [buffer_pos];
					next_handle_index++;
				} else {
					break;
				}
			}
		}
		line_pos++;
		buffer_pos++;

		if (current_handle != NULL) {
			current_handle->buffer_size++;
		}

		if (ch == '\n' || ch == '\r') {
			is_cr_lf = TRUE;
		} else {
			is_cr_lf = FALSE;
		}
	}
	return next_handle_index;
}

/**
 * fu_ccgx_cyacd_file_set_pos
 *
 * @handle: Cyacd File Handle
 * @pos	cyacd data buffer
 *
 * Set position	in cyacd buffer in cyacd handle
 */
void
fu_ccgx_cyacd_file_set_pos (CyacdFileHandle *handle, guint32 pos)
{
	if (handle != NULL) {
		handle->pos = pos;
	}
}

/**
 * fu_ccgx_cyacd_file_get_pos:
 * @handle: Cyacd File Handle
 *
 * Get position in cyacd buffer in cyacd handle
 *
 * Returns: position in n cyacd buffer
 */

guint32
fu_ccgx_cyacd_file_get_pos (CyacdFileHandle *handle)
{
	guint32 pos = 0;
	if (handle != NULL) {
		pos = handle->pos;
	}
	return pos;
}

static gboolean
cyacd_buffer_read_header (CyacdFileHandle *handle, guint16 *silicon_id)
{
	guint8 hex_buffer[4] = {0};
	g_autofree guint8 *ascii_buffer = g_malloc0 (CYACD_ROW_ASCII_BUFFER_SIZE);

	g_return_val_if_fail (ascii_buffer != NULL , FALSE);
	g_return_val_if_fail (silicon_id != NULL , FALSE);
	fu_ccgx_cyacd_file_set_pos(handle, 0);

	if (cyacd_buffer_read_line (handle, ascii_buffer, CYACD_ROW_ASCII_BUFFER_SIZE)) {
		convert_ascii_to_hex (ascii_buffer, sizeof(ascii_buffer), hex_buffer, sizeof(hex_buffer));
		*silicon_id = hex_buffer[0]<<8 | hex_buffer[1];
		return TRUE;
	}
	return FALSE;
}

static guint8
calculate_byte_checksum(guint8 *ptr, guint32 size)
{
	guint8 checksum = 0;
	guint32 index;

	g_return_val_if_fail (ptr != NULL && size !=0, 0);

	/* calculate the binary sum of all the data */
	for (index = 0; index < size; index++)
		checksum += ptr[index];

	/* return the 2's complement of the binary sum */
	return ((guint8)(1u) + (guint8)(~checksum));
}

static gboolean
cyacd_buffer_read_row_raw_data(CyacdFileHandle *handle, guint8 *data, guint32 size)
{
	g_autofree guint8 *ascii_buffer = g_malloc0(CYACD_ROW_ASCII_BUFFER_SIZE);
	guint32 read_size = 0;

	g_return_val_if_fail (ascii_buffer != NULL, 0);
	read_size = cyacd_buffer_read_line(handle, ascii_buffer, CYACD_ROW_ASCII_BUFFER_SIZE);

	if (read_size > 0) {
		if ((ascii_buffer[0] == ':') && (ascii_buffer[1] == '0') && (ascii_buffer[2] == '0')) {
			if (size > ((read_size - 1) / 2))  /* 1 = 1 (:) */
				size = (read_size - 1) / 2;

			/* convert ASCII to HEX data and ignoring first entry */
			convert_ascii_to_hex(&ascii_buffer[1], read_size - 1, data, size);
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * fu_ccgx_cyacd_file_parse:
 * @handle: Cyacd File Handle
 * @info[out] Cyacd file information
 *
 * Parse cyacd file and return cyacd file information
 *
 * Returns: %TRUE for success
 */
gboolean
fu_ccgx_cyacd_file_parse (CyacdFileHandle *handle, CyacdFileInfo *info)
{
	guint16	silicon_id;
	guint32	row_data_start_pos = 0;
	guint16	row_num = 0;
	guint16	row_size = 0;
	guint8*	row_data = 0;
	guint32	row_max = 0;
	CCGxPartInfo *ccgx_info = NULL;
	guint32	fw1_meta_row_num;
	guint32	fw2_meta_row_num;
	guint32	row_index = 0;
	guint32	version_row_index = 0;
	guint32	version_row_offset = 0;
	gint32	version_row_num = -1;
	gboolean version_found;
	gboolean fw_mode_found;
	guint32	fw_meta_offset = 0;
	g_autofree guint8 *row_raw_buffer = g_malloc0 (CYACD_ROW_ASCII_BUFFER_SIZE/2);
	guint32 line_size = 0;
	guint8  line_checksum = 0;
	guint8  checksum = 0;
	guint8  full_checksum = 0;
	guint32 fw_size = 0;
	guint32 index = 0;

	g_return_val_if_fail (handle != NULL , FALSE);
	g_return_val_if_fail (info != NULL , FALSE);
	g_return_val_if_fail (row_raw_buffer != NULL , FALSE);

	/* set to start position */
	fu_ccgx_cyacd_file_set_pos (handle, 0);

	/* read header */
	if (cyacd_buffer_read_header (handle, &silicon_id) == FALSE) {
		return FALSE;
	}

	/* get ccgx information */
	ccgx_info = fu_ccgx_util_find_ccgx_info (silicon_id);
	if (ccgx_info == NULL) {
		return FALSE;
	}

	/* make 16 bit silicon id */
	info->silicon_id = silicon_id;
	info->row_size = ccgx_info->flash_row_size;

	row_max = ccgx_info->flash_size / ccgx_info->flash_row_size;
	if (info->row_size > CYACD_FLASH_ROW_MAX) {
		return FALSE;
	}

	fw1_meta_row_num = row_max-1;
	fw2_meta_row_num = row_max-2;

	if (ccgx_info->flash_row_size == 128) {
		fw_meta_offset = 64;
	} else if (ccgx_info->flash_row_size == 256) {
		fw_meta_offset = 128 + 64;
	} else {
		fw_meta_offset = 0;
		return FALSE;
	}

	/* support CCG3/CCG4/CCG5 Only */
	if (	(g_strcmp0 ("CCG3", (const char*)ccgx_info->family_name) != 0) &&
		(g_strcmp0 ("CCG4", (const char*)ccgx_info->family_name) != 0) &&
		(g_strcmp0 ("CCG5", (const char*)ccgx_info->family_name) != 0)) {
		return FALSE;
	}
/* CCG2	not support
	if (strcmp("CCG2", (const char*)ccgx_info->family_name)	== 0) {
		version_row_num	= CCG2_APP_VERSION_ROW_NUM;
		version_row_offset = 4;
	}
*/
	version_row_index = CCGX_APP_VERSION_OFFSET / ccgx_info->flash_row_size;
	version_row_offset = CCGX_APP_VERSION_OFFSET % ccgx_info->flash_row_size;

	row_index = 0;
	version_found = FALSE;
	fw_mode_found = FALSE;

	row_data_start_pos = handle->pos;

	while (cyacd_buffer_read_row_raw_data (handle, row_raw_buffer, CYACD_ROW_BUFFER_SIZE)) {

		row_num = ((row_raw_buffer[1] << 8) | (row_raw_buffer[2]));
		row_size = ((row_raw_buffer[3] << 8) | (row_raw_buffer[4]));
		row_data = &row_raw_buffer[5];
		line_size = 5 + row_size; /* array id(1) + row num(2) + row size(2) + data(n) */

		if (row_size != ccgx_info->flash_row_size) {
			g_warning ("flash row size mismatch");
			break;
		}

		/* check line checksum */
		line_checksum = calculate_byte_checksum(row_raw_buffer, line_size);
		if (line_checksum != row_data[row_size]) {
			g_warning ("cyacd line checksum error 0x%X != 0x%X", line_checksum, row_data[row_size]);
			break;
		}

		if (row_num != fw1_meta_row_num && row_num != fw2_meta_row_num) {
			/* calculate the binary sum of all the data */
			for (index = 0; index < row_size; index++) {
				checksum += row_data[index];
			}
			fw_size += row_size;
		}

		if (version_row_num < 0 && row_index == 0) {
			version_row_num = row_num + version_row_index;
		}

		if (version_row_num == row_num) {
			info->app_version.val= row_data [version_row_offset + 3] << 24 |
							 row_data[version_row_offset + 2] << 16 |
							 row_data[version_row_offset + 1] << 8 |
							 row_data[version_row_offset + 0];
			version_found = TRUE;
		}

		row_index++;

		if (row_num == fw1_meta_row_num || row_num == fw2_meta_row_num) {

			if (row_num == fw1_meta_row_num)
				info->fw_mode = FW_MODE_FW1;
			else
				info->fw_mode = FW_MODE_FW2;

			memcpy (&info->fw_metadata, &row_data [fw_meta_offset], sizeof(CCGxMetaData));

			full_checksum = ((guint8)(1u) + (guint8)(~checksum));  /* the 2's complement of the binary sum. */
			if (full_checksum != info->fw_metadata.fw_checksum) {
				g_warning ("cyacd fw checksum error 0x%X != 0x%X", full_checksum, info->fw_metadata.fw_checksum);
				break;
			}

			if (fw_size != info->fw_metadata.fw_size) {
				g_warning ("cyacd fw size error %d != %d", (gint32)fw_size, (gint32)info->fw_metadata.fw_size);
				break;
			}

			if (CCGX_METADATA_VALID_SIG != info->fw_metadata.metadata_valid) {
				g_warning ("cyacd meta valid error 0x%02X != 0x%02X", (guint32)CCGX_METADATA_VALID_SIG, (guint32)info->fw_metadata.metadata_valid);
				break;
			}
			fw_mode_found =	TRUE;

			break;
		}
	}

	if (version_found && fw_mode_found) {
		/* set to row data start position */
		fu_ccgx_cyacd_file_set_pos (handle, row_data_start_pos);
		return TRUE;
	}
	return FALSE;
}

/**
 * fu_ccgx_cyacd_file_read_row:
 * @handle: Cyacd File Handle
 * @data[in,out] cyacd data buffer
 * @size Maximun size of cyacd data buffer
 *
 * Read row data in cyacd buffer
 *
 * Returns: %TRUE for success
 */
gboolean
fu_ccgx_cyacd_file_read_row (CyacdFileHandle *handle, guint8 *data, guint32 size)
{
	guint32 read_size = 0;
	g_autofree guint8 *ascii_buffer = g_malloc0 (CYACD_ROW_ASCII_BUFFER_SIZE);

	g_return_val_if_fail (handle != NULL ,	FALSE);
	g_return_val_if_fail (data != NULL , FALSE);
	g_return_val_if_fail (ascii_buffer != NULL , FALSE);

	read_size = cyacd_buffer_read_line (handle, ascii_buffer, CYACD_ROW_ASCII_BUFFER_SIZE);

	if (read_size > 0) {
		if (ascii_buffer[0] == ':' && ascii_buffer[1] == '0' && ascii_buffer[2] == '0') { /* : and 1byte array_id */
			if (size > ((read_size - 5) / 2)) { /* 5 = 1 (:) + 2(array_id) + 2(checksum) */
				size = (read_size - 5) / 2;
			}

			/* convert ASCII data in HEX data.
			 * as per .cyacd format, first byte is :, second byte is
			 * row ID and last is checksum. Ignoring these two enteries*/

			convert_ascii_to_hex (&ascii_buffer[3], read_size - 3, data, size);

			/* first two enteries : row_num and row_length are
			 * stored in big endian format in file. wwap them to
			 * little endian. */

			*((guint16 *)&data[0]) = ((data[0] << 8) | (data[1]));
			*((guint16 *)&data[2]) = ((data[2] << 8) | (data[3]));

			return TRUE;
		}
	}
	return FALSE;
}
