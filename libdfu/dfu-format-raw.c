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
#include "dfu-format-raw.h"
#include "dfu-image.h"
#include "dfu-error.h"

/**
 * dfu_firmware_detect_raw: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_RAW
 **/
DfuFirmwareFormat
dfu_firmware_detect_raw (GBytes *bytes)
{
	return DFU_FIRMWARE_FORMAT_RAW;
}

/**
 * dfu_firmware_from_raw: (skip)
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
dfu_firmware_from_raw (DfuFirmware *firmware,
		       GBytes *bytes,
		       DfuFirmwareParseFlags flags,
		       GError **error)
{
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(DfuImage) image = NULL;
	image = dfu_image_new ();
	element = dfu_element_new ();
	dfu_element_set_contents (element, bytes);
	dfu_image_add_element (image, element);
	dfu_firmware_add_image (firmware, image);
	return TRUE;
}

/**
 * dfu_firmware_to_raw: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs raw firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_raw (DfuFirmware *firmware, GError **error)
{
	DfuElement *element;
	DfuImage *image;
	GBytes *contents;

	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "no firmware image data to write");
		return NULL;
	}
	element = dfu_image_get_element (image, 0);
	if (element == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "no firmware element data to write");
		return NULL;
	}
	contents = dfu_element_get_contents (element);
	return g_bytes_ref (contents);
}
