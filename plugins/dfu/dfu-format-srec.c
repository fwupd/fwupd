/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "dfu-element.h"
#include "dfu-firmware.h"
#include "dfu-format-srec.h"
#include "dfu-image.h"

#include "fwupd-error.h"

/**
 * dfu_firmware_detect_srec: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_RAW
 **/
DfuFirmwareFormat
dfu_firmware_detect_srec (GBytes *bytes)
{
	guint8 *data;
	gsize len;
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (len < 12)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;
	if (memcmp (data, "S0", 2) != 0)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;
	return DFU_FIRMWARE_FORMAT_SREC;
}

typedef enum {
	DFU_SREC_RECORD_CLASS_UNKNOWN,
	DFU_SREC_RECORD_CLASS_HEADER,
	DFU_SREC_RECORD_CLASS_DATA,
	DFU_SREC_RECORD_CLASS_TERMINATION,
	DFU_SREC_RECORD_CLASS_COUNT,
	DFU_SREC_RECORD_CLASS_LAST
} DfuSrecClassType;

/**
 * dfu_firmware_from_srec: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from raw data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_image_from_srec (DfuImage *image,
		     GBytes *bytes,
		     guint32 start_addr,
		     DfuFirmwareParseFlags flags,
		     GError **error)
{
	const gchar *in_buffer;
	gboolean got_eof = FALSE;
	gboolean got_hdr = FALSE;
	gsize len_in;
	guint16 class_data_cnt = 0;
	guint32 addr32_last = 0;
	guint32 element_address = 0;
	guint offset = 0;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GString) modname = g_string_new (NULL);
	g_autoptr(GString) outbuf = NULL;

	g_return_val_if_fail (bytes != NULL, FALSE);

	/* create element */
	element = dfu_element_new ();

	/* parse records */
	in_buffer = g_bytes_get_data (bytes, &len_in);
	outbuf = g_string_new ("");
	while (offset < len_in) {
		DfuSrecClassType rec_class = DFU_SREC_RECORD_CLASS_UNKNOWN;
		guint32 rec_addr32;
		guint8 rec_count;		/* bytes */
		guint8 rec_dataoffset;		/* bytes */
		guint8 rec_kind;

		/* check starting token */
		if (in_buffer[offset] != 'S') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token, got 0x%02x at 0x%x",
				     (guint) in_buffer[offset], offset);
			return FALSE;
		}

		/* check there's enough data for the smallest possible record */
		if (offset + 10 > (guint) len_in) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "record incomplete at %u, length %u",
				     offset, (guint) len_in);
			return FALSE;
		}

		/* kind, count, address, (data), checksum, linefeed */
		rec_kind = in_buffer[offset + 1];
		rec_count = dfu_utils_buffer_parse_uint8 (in_buffer + offset + 2);

		/* check we can read out this much data */
		if (len_in < offset + (rec_count * 2) + 4) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "file incomplete at %u, length %u",
				     offset, (guint) len_in);
			return FALSE;
		}

		/* checksum check */
		if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST) == 0) {
			guint8 rec_csum = 0;
			guint8 rec_csum_expected;
			for (guint8 i = 0; i < rec_count; i++)
				rec_csum += dfu_utils_buffer_parse_uint8 (in_buffer + offset + (i * 2) + 2);
			rec_csum ^= 0xff;
			rec_csum_expected = dfu_utils_buffer_parse_uint8 (in_buffer + offset + (rec_count * 2) + 2);
			if (rec_csum != rec_csum_expected) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "checksum incorrect @ 0x%04x, expected %02x, got %02x",
					     offset, rec_csum_expected, rec_csum);
				return FALSE;
			}
		}

		/* record kind + record count (in bytes, not chars) */
		rec_dataoffset = 2;

		/* parse record */
		switch (rec_kind) {
		case '0':
			rec_class = DFU_SREC_RECORD_CLASS_HEADER;
			rec_dataoffset += 2;
			rec_addr32 = dfu_utils_buffer_parse_uint16 (in_buffer + offset + 4);
			if (got_hdr) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "duplicate header record");
				return FALSE;
			}
			if (rec_addr32 != 0x0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid header record address, got %04x",
					     rec_addr32);
				return FALSE;
			}
			/* could be anything, lets assume text */
			for (guint8 i = rec_dataoffset; i <= rec_count; i++) {
				guint8 tmp = dfu_utils_buffer_parse_uint8 (in_buffer + offset + (i * 2));
				if (!g_ascii_isgraph (tmp))
					break;
				g_string_append_c (modname, tmp);
			}
			if (modname->len != 0)
				dfu_image_set_name (image, modname->str);
			got_hdr = TRUE;
			break;
		case '1':
			rec_class = DFU_SREC_RECORD_CLASS_DATA;
			rec_dataoffset += 2;
			rec_addr32 = dfu_utils_buffer_parse_uint16 (in_buffer + offset + 4);
			break;
		case '2':
			rec_class = DFU_SREC_RECORD_CLASS_DATA;
			rec_dataoffset += 3;
			rec_addr32 = dfu_utils_buffer_parse_uint24 (in_buffer + offset + 4);
			break;
		case '3':
			rec_class = DFU_SREC_RECORD_CLASS_DATA;
			rec_dataoffset += 4;
			rec_addr32 = dfu_utils_buffer_parse_uint32 (in_buffer + offset + 4);
			break;
		case '9':
			rec_class = DFU_SREC_RECORD_CLASS_TERMINATION;
			rec_addr32 = dfu_utils_buffer_parse_uint16 (in_buffer + offset + 4);
			got_eof = TRUE;
			break;
		case '8':
			rec_class = DFU_SREC_RECORD_CLASS_TERMINATION;
			rec_addr32 = dfu_utils_buffer_parse_uint24 (in_buffer + offset + 4);
			got_eof = TRUE;
			break;
		case '7':
			rec_class = DFU_SREC_RECORD_CLASS_TERMINATION;
			rec_addr32 = dfu_utils_buffer_parse_uint32 (in_buffer + offset + 4);
			got_eof = TRUE;
			break;
		case '5':
			rec_class = DFU_SREC_RECORD_CLASS_COUNT;
			rec_addr32 = dfu_utils_buffer_parse_uint16 (in_buffer + offset + 4);
			if (rec_addr32 != class_data_cnt) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "count record was not valid, got 0x%02x expected 0x%02x",
					     (guint) rec_addr32, (guint) class_data_cnt);
				return FALSE;
			}
			got_eof = TRUE;
			break;
		default:
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid srec record type S%c",
				     rec_kind);
			return FALSE;
		}

		/* record EOF */
		if (rec_class == DFU_SREC_RECORD_CLASS_TERMINATION)
			g_debug ("start execution location: 0x%04x", (guint) rec_addr32);

		/* read data */
		if (rec_class == DFU_SREC_RECORD_CLASS_DATA) {
			/* probably invalid data */
			if (!got_hdr) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "missing header record");
				return FALSE;
			}
			/* does not make sense */
			if (rec_addr32 < addr32_last) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid address 0x%x, last was 0x%x",
					     (guint) rec_addr32,
					     (guint) addr32_last);
				return FALSE;
			}
			if (rec_addr32 < start_addr) {
				g_debug ("ignoring data at 0x%x as before start address 0x%x",
					 (guint) rec_addr32, (guint) start_addr);
			} else {
				for (guint8 i = rec_dataoffset; i <= rec_count; i++) {
					guint8 tmp = dfu_utils_buffer_parse_uint8 (in_buffer + offset + (i * 2));
					g_string_append_c (outbuf, tmp);
				}
				if (element_address == 0x0)
					element_address = rec_addr32;
			}
			addr32_last = rec_addr32++;
			class_data_cnt++;
		}

		/* ignore any line return */
		offset += (rec_count * 2) + 4;
		for (; offset < len_in; offset++) {
			if (in_buffer[offset] != '\n' &&
			    in_buffer[offset] != '\r')
				break;
		}
	}

	/* no EOF */
	if (!got_eof) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no EOF, perhaps truncated file");
		return FALSE;
	}

	/* add single image */
	contents = g_bytes_new (outbuf->str, outbuf->len);
	dfu_element_set_contents (element, contents);
	dfu_element_set_address (element, element_address);
	dfu_image_add_element (image, element);
	return TRUE;
}

/**
 * dfu_firmware_from_srec: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from raw data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_srec (DfuFirmware *firmware,
			GBytes *bytes,
			DfuFirmwareParseFlags flags,
			GError **error)
{
	g_autoptr(DfuImage) image = NULL;

	g_return_val_if_fail (bytes != NULL, FALSE);

	/* add single image */
	image = dfu_image_new ();
	if (!dfu_image_from_srec (image, bytes, 0x0, flags, error))
		return FALSE;
	dfu_firmware_add_image (firmware, image);
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_SREC);
	return TRUE;
}

/**
 * dfu_firmware_to_srec: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Exports a Motorola S-record file
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_srec (DfuFirmware *firmware, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Motorola S-record export functionality missing");
	return NULL;
}
