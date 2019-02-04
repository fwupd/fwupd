/*
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
	const gchar *data;
	gboolean got_eof = FALSE;
	gboolean got_hdr = FALSE;
	gsize sz = 0;
	guint16 data_cnt = 0;
	guint32 addr32_last = 0;
	guint32 element_address = 0;
	g_auto(GStrv) lines = NULL;
	g_autoptr(DfuElement) element = dfu_element_new ();
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GString) outbuf = g_string_new (NULL);

	g_return_val_if_fail (bytes != NULL, FALSE);

	/* parse records */
	data = g_bytes_get_data (bytes, &sz);
	lines = dfu_utils_strnsplit (data, sz, "\n", -1);
	for (guint ln = 0; lines[ln] != NULL; ln++) {
		const gchar *line = lines[ln];
		gsize linesz;
		guint32 rec_addr32;
		guint8 addrsz = 0;		/* bytes */
		guint8 rec_count;		/* words */
		guint8 rec_kind;

		/* ignore blank lines */
		g_strdelimit (lines[ln], "\r", '\0');
		linesz = strlen (line);
		if (linesz == 0)
			continue;

		/* check starting token */
		if (line[0] != 'S') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token, got '%c' at line %u",
				     line[0], ln);
			return FALSE;
		}

		/* check there's enough data for the smallest possible record */
		if (linesz < 10) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "record incomplete at line %u, length %u",
				     ln, (guint) linesz);
			return FALSE;
		}

		/* kind, count, address, (data), checksum, linefeed */
		rec_kind = line[1] - '0';
		rec_count = dfu_utils_buffer_parse_uint8 (line + 2);
		if (rec_count * 2 != linesz - 4) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "count incomplete at line %u, "
				     "length %u, expected %u",
				     ln, (guint) linesz - 4, (guint) rec_count * 2);
			return FALSE;
		}

		/* checksum check */
		if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST) == 0) {
			guint8 rec_csum = 0;
			guint8 rec_csum_expected;
			for (guint8 i = 0; i < rec_count; i++)
				rec_csum += dfu_utils_buffer_parse_uint8 (line + (i * 2) + 2);
			rec_csum ^= 0xff;
			rec_csum_expected = dfu_utils_buffer_parse_uint8 (line + (rec_count * 2) + 2);
			if (rec_csum != rec_csum_expected) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "checksum incorrect line %u, "
					     "expected %02x, got %02x",
					     ln, rec_csum_expected, rec_csum);
				return FALSE;
			}
		}

		/* set each command settings */
		switch (rec_kind) {
		case 0:
			addrsz = 2;
			if (got_hdr) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "duplicate header record");
				return FALSE;
			}
			got_hdr = TRUE;
			break;
		case 1:
			addrsz = 2;
			break;
		case 2:
			addrsz = 3;
			break;
		case 3:
			addrsz = 4;
			break;
		case 5:
			addrsz = 2;
			got_eof = TRUE;
			break;
		case 6:
			addrsz = 3;
			break;
		case 7:
			addrsz = 4;
			got_eof = TRUE;
			break;
		case 8:
			addrsz = 3;
			got_eof = TRUE;
			break;
		case 9:
			addrsz = 2;
			got_eof = TRUE;
			break;
		default:
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid srec record type S%c",
				     line[1]);
			return FALSE;
		}

		/* parse address */
		switch (addrsz) {
		case 2:
			rec_addr32 = dfu_utils_buffer_parse_uint16 (line + 4);
			break;
		case 3:
			rec_addr32 = dfu_utils_buffer_parse_uint24 (line + 4);
			break;
		case 4:
			rec_addr32 = dfu_utils_buffer_parse_uint32 (line + 4);
			break;
		default:
			g_assert_not_reached ();
		}

		/* header */
		if (rec_kind == 0) {
			g_autoptr(GString) modname = g_string_new (NULL);
			if (rec_addr32 != 0x0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid header record address, got %04x",
					     rec_addr32);
				return FALSE;
			}

			/* could be anything, lets assume text */
			for (guint8 i = 4 + (addrsz * 2); i <= rec_count * 2; i += 2) {
				guint8 tmp = dfu_utils_buffer_parse_uint8 (line + i);
				if (!g_ascii_isgraph (tmp))
					break;
				g_string_append_c (modname, tmp);
			}
			if (modname->len != 0)
				dfu_image_set_name (image, modname->str);
			continue;
		}

		/* verify we got all records */
		if (rec_kind == 5) {
			if (rec_addr32 != data_cnt) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "count record was not valid, got 0x%02x expected 0x%02x",
					     (guint) rec_addr32, (guint) data_cnt);
				return FALSE;
			}
		}

		/* data */
		if (rec_kind == 1 || rec_kind == 2 || rec_kind == 3) {
			/* invalid */
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
				guint bytecnt = 0;
				guint32 len_hole = rec_addr32 - addr32_last;

				/* fill any holes, but only up to 1Mb to avoid a DoS */
				if (addr32_last > 0 && len_hole > 0x100000) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "hole of 0x%x bytes too large to fill",
						     (guint) len_hole);
					return FALSE;
				}
				if (addr32_last > 0x0 && len_hole > 1) {
					g_debug ("filling address 0x%08x to 0x%08x",
						 addr32_last + 1, addr32_last + len_hole - 1);
					for (guint j = 0; j < len_hole; j++)
						g_string_append_c (outbuf, 0xff);
				}

				/* add data */
				for (guint8 i = 4 + (addrsz * 2); i <= rec_count * 2; i += 2) {
					guint8 tmp = dfu_utils_buffer_parse_uint8 (line + i);
					g_string_append_c (outbuf, tmp);
					bytecnt++;
				}
				if (element_address == 0x0)
					element_address = rec_addr32;
				addr32_last = rec_addr32 + bytecnt;
			}
			data_cnt++;
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
