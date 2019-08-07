/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "dfu-element.h"
#include "dfu-firmware.h"
#include "dfu-format-ihex.h"
#include "dfu-image.h"

#include "fu-firmware-common.h"
#include "fu-ihex-firmware.h"

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

	/* match the first char */
	if (data[0] == ':')
		return DFU_FIRMWARE_FORMAT_INTEL_HEX;

	/* look for the EOF line */
	if (g_strstr_len ((const gchar *) data, (gssize) len, ":000000") != NULL)
		return DFU_FIRMWARE_FORMAT_INTEL_HEX;

	/* failed */
	return DFU_FIRMWARE_FORMAT_UNKNOWN;
}

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
	g_autoptr(FuFirmware) firmware_new = fu_ihex_firmware_new ();
	g_autoptr(GPtrArray) imgs = NULL;
	FwupdInstallFlags flags_new = FWUPD_INSTALL_FLAG_NONE;

	/* make a native objects from the abstract firmware */
	if (flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST)
		flags_new |= FWUPD_INSTALL_FLAG_FORCE;
	if (!fu_firmware_parse (firmware_new, bytes, flags_new, error))
		return FALSE;
	imgs = fu_firmware_get_images (firmware_new);
	for (guint i = 0; i  < imgs->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (imgs, i);
		g_autoptr(DfuElement) element = dfu_element_new ();
		g_autoptr(DfuImage) image = dfu_image_new ();
		dfu_element_set_contents (element, fu_firmware_image_get_bytes (img, NULL));
		dfu_element_set_address (element, fu_firmware_image_get_addr (img));
		dfu_image_add_element (image, element);
		dfu_image_set_name (image, "ihex");
		dfu_firmware_add_image (firmware, image);

	}
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_INTEL_HEX);
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
	g_autoptr(FuFirmware) firmware_new = fu_ihex_firmware_new ();

	/* make a new object from the native firmware */
	images = dfu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		DfuImage *image = g_ptr_array_index (images, i);
		GPtrArray *elements = dfu_image_get_elements (image);
		for (guint j = 0; j < elements->len; j++) {
			DfuElement *element = g_ptr_array_index (elements, j);
			g_autoptr(FuFirmwareImage) img = NULL;
			img = fu_firmware_image_new (dfu_element_get_contents (element));
			fu_firmware_image_set_id (img, dfu_image_get_name (image));
			fu_firmware_image_set_addr (img, dfu_element_get_address (element));
			fu_firmware_add_image (firmware_new, img);
		}
	}
	return fu_firmware_write (firmware_new, error);
}
