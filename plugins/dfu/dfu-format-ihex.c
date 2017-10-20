/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>

#include "dfu-element.h"
#include "dfu-firmware.h"
#include "dfu-format-ihex.h"
#include "dfu-image.h"

#include "fwupd-error.h"

/**
 * dfu_firmware_detect_ihex: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_RAW
 **/
DfuFirmwareFormat
dfu_firmware_detect_ihex (GBytes *bytes)
{
	guint8 *data;
	gsize len;
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (len < 12)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;
	if (data[0] != ':')
		return DFU_FIRMWARE_FORMAT_UNKNOWN;
	return DFU_FIRMWARE_FORMAT_INTEL_HEX;
}

static guint8
dfu_firmware_ihex_parse_uint8 (const gchar *data, guint pos)
{
	gchar buffer[3];
	memcpy (buffer, data + pos, 2);
	buffer[2] = '\0';
	return (guint8) g_ascii_strtoull (buffer, NULL, 16);
}

static guint16
dfu_firmware_ihex_parse_uint16 (const gchar *data, guint pos)
{
	gchar buffer[5];
	memcpy (buffer, data + pos, 4);
	buffer[4] = '\0';
	return (guint16) g_ascii_strtoull (buffer, NULL, 16);
}

static guint32
dfu_firmware_ihex_parse_uint32 (const gchar *data, guint pos)
{
	gchar buffer[9];
	memcpy (buffer, data + pos, 8);
	buffer[8] = '\0';
	return (guint32) g_ascii_strtoull (buffer, NULL, 16);
}

#define	DFU_INHX32_RECORD_TYPE_DATA		0x00
#define	DFU_INHX32_RECORD_TYPE_EOF		0x01
#define	DFU_INHX32_RECORD_TYPE_EXTENDED		0x04
#define	DFU_INHX32_RECORD_TYPE_ADDR32		0x05
#define	DFU_INHX32_RECORD_TYPE_SIGNATURE	0xfd

/**
 * dfu_firmware_from_ihex: (skip)
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
dfu_firmware_from_ihex (DfuFirmware *firmware,
			GBytes *bytes,
			DfuFirmwareParseFlags flags,
			GError **error)
{
	const gchar *in_buffer;
	gboolean got_eof = FALSE;
	gsize len_in;
	guint16 addr_high = 0;
	guint16 addr_low = 0;
	guint32 addr32 = 0;
	guint32 addr32_last = 0;
	guint32 element_address = 0;
	guint8 checksum;
	guint8 data_tmp;
	guint8 len_tmp;
	guint8 type;
	guint end;
	guint offset = 0;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GString) string = NULL;
	g_autoptr(GString) signature = g_string_new (NULL);

	g_return_val_if_fail (bytes != NULL, FALSE);

	/* create element */
	image = dfu_image_new ();
	dfu_image_set_name (image, "ihex");
	element = dfu_element_new ();

	/* parse records */
	in_buffer = g_bytes_get_data (bytes, &len_in);
	string = g_string_new ("");
	while (offset < len_in) {

		/* check starting token */
		if (in_buffer[offset] != ':') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token, got %c at %x",
				     in_buffer[offset], offset);
			return FALSE;
		}

		/* check there's enough data for the smallest possible record */
		if (offset + 12 > (guint) len_in) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "record incomplete at %u, length %u",
				     offset, (guint) len_in);
			return FALSE;
		}

		/* length, 16-bit address, type */
		len_tmp = dfu_firmware_ihex_parse_uint8 (in_buffer, offset+1);
		addr_low = dfu_firmware_ihex_parse_uint16 (in_buffer, offset+3);
		type = dfu_firmware_ihex_parse_uint8 (in_buffer, offset+7);

		/* position of checksum */
		end = offset + 9 + len_tmp * 2;
		if (end > (guint) len_in) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "checksum > file length: %u",
				     end);
			return FALSE;
		}

		/* verify checksum */
		if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST) == 0) {
			checksum = 0;
			for (guint i = offset + 1; i < end + 2; i += 2) {
				data_tmp = dfu_firmware_ihex_parse_uint8 (in_buffer, i);
				checksum += data_tmp;
			}
			if (checksum != 0)  {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "invalid record checksum");
				return FALSE;
			}
		}

		/* process different record types */
		switch (type) {
		case DFU_INHX32_RECORD_TYPE_DATA:
			/* if not contiguous with previous record */
			if ((addr_high + addr_low) != addr32) {
				if (addr32 == 0x0) {
					g_debug ("base address %08x", addr_low);
					dfu_element_set_address (element, addr_low);
				}
				addr32 = ((guint32) addr_high << 16) + addr_low;
				if (element_address == 0x0)
					element_address = addr32;
			}

			/* does not make sense */
			if (addr32 < addr32_last) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid address 0x%x, last was 0x%x",
					     (guint) addr32,
					     (guint) addr32_last);
				return FALSE;
			}

			/* parse bytes from line */
			g_debug ("writing data 0x%08x", (guint32) addr32);
			for (guint i = offset + 9; i < end; i += 2) {
				/* any holes in the hex record */
				guint32 len_hole = addr32 - addr32_last;
				if (addr32_last > 0x0 && len_hole > 1) {
					for (guint j = 1; j < len_hole; j++) {
						g_debug ("filling address 0x%08x",
							 addr32_last + j);
						/* although 0xff might be clearer,
						 * we can't write 0xffff to pic14 */
						g_string_append_c (string, 0x00);
					}
				}
				/* write into buf */
				data_tmp = dfu_firmware_ihex_parse_uint8 (in_buffer, i);
				g_string_append_c (string, (gchar) data_tmp);
				addr32_last = addr32++;
			}
			break;
		case DFU_INHX32_RECORD_TYPE_EOF:
			if (got_eof) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "duplicate EOF, perhaps "
						     "corrupt file");
				return FALSE;
			}
			got_eof = TRUE;
			break;
		case DFU_INHX32_RECORD_TYPE_EXTENDED:
			addr_high = dfu_firmware_ihex_parse_uint16 (in_buffer, offset+9);
			addr32 = ((guint32) addr_high << 16) + addr_low;
			break;
		case DFU_INHX32_RECORD_TYPE_ADDR32:
			addr32 = dfu_firmware_ihex_parse_uint32 (in_buffer, offset+9);
			break;
		case DFU_INHX32_RECORD_TYPE_SIGNATURE:
			for (guint i = offset + 9; i < end; i += 2) {
				guint8 tmp_c = dfu_firmware_ihex_parse_uint8 (in_buffer, i);
				g_string_append_c (signature, tmp_c);
			}
			break;
		default:
			/* vendors sneak in nonstandard sections past the EOF */
			if (got_eof)
				break;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid ihex record type %i",
				     type);
			return FALSE;
		}

		/* ignore any line return */
		offset = end + 2;
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
	contents = g_bytes_new (string->str, string->len);
	dfu_element_set_contents (element, contents);
	dfu_element_set_address (element, element_address);
	dfu_image_add_element (image, element);
	dfu_firmware_add_image (firmware, image);
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_INTEL_HEX);

	/* add optional signature */
	if (signature->len > 0) {
		g_autoptr(DfuElement) element_sig = dfu_element_new ();
		g_autoptr(DfuImage) image_sig = dfu_image_new ();
		g_autoptr(GBytes) data = g_bytes_new_static (signature->str, signature->len);
		dfu_element_set_contents (element_sig, data);
		dfu_image_add_element (image_sig, element_sig);
		dfu_image_set_name (image_sig, "signature");
		dfu_firmware_add_image (firmware, image_sig);
	}
	return TRUE;
}

static void
dfu_firmware_to_ihex_bytes (GString *str, guint8 record_type,
			    guint32 address, GBytes *contents)
{
	const guint8 *data;
	const guint chunk_size = 16;
	gsize len;

	/* get number of chunks */
	data = g_bytes_get_data (contents, &len);
	for (gsize i = 0; i < len; i += chunk_size) {
		guint8 checksum = 0;

		/* length, 16-bit address, type */
		gsize chunk_len = MIN (len - i, 16);
		g_string_append_printf (str, ":%02X%04X%02X",
					(guint) chunk_len,
					(guint) (address + i),
					(guint) record_type);
		for (gsize j = 0; j < chunk_len; j++)
			g_string_append_printf (str, "%02X", data[i+j]);

		/* add checksum */
		for (gsize j = 0; j < (chunk_len * 2) + 8; j++)
			checksum += (guint8) str->str[str->len - (j + 1)];
		g_string_append_printf (str, "%02X\n", checksum);
	}
}

static gboolean
dfu_firmware_to_ihex_element (DfuElement *element, GString *str,
			      guint8 record_type, GError **error)
{
	GBytes *contents = dfu_element_get_contents (element);
	dfu_firmware_to_ihex_bytes (str, record_type,
				    dfu_element_get_address (element),
				    contents);
	return TRUE;
}

static gboolean
dfu_firmware_to_ihex_image (DfuImage *image, GString *str, GError **error)
{
	GPtrArray *elements;
	guint8 record_type = DFU_INHX32_RECORD_TYPE_DATA;

	if (g_strcmp0 (dfu_image_get_name (image), "signature") == 0)
		record_type = DFU_INHX32_RECORD_TYPE_SIGNATURE;
	elements = dfu_image_get_elements (image);
	for (guint i = 0; i < elements->len; i++) {
		DfuElement *element = g_ptr_array_index (elements, i);
		if (!dfu_firmware_to_ihex_element (element,
						   str,
						   record_type,
						   error))
			return FALSE;
	}
	return TRUE;
}

/**
 * dfu_firmware_to_ihex: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs a IHEX firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_ihex (DfuFirmware *firmware, GError **error)
{
	GPtrArray *images;
	g_autoptr(GString) str = NULL;

	/* write all the element data */
	str = g_string_new ("");
	images = dfu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		DfuImage *image = g_ptr_array_index (images, i);
		if (!dfu_firmware_to_ihex_image (image, str, error))
			return NULL;
	}

	/* add EOF */
	g_string_append_printf (str, ":000000%02XFF\n",
				(guint) DFU_INHX32_RECORD_TYPE_EOF);

	return g_bytes_new (str->str, str->len);
}
