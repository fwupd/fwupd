/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "dfu-firmware.h"
#include "dfu-format-srec.h"
#include "dfu-image.h"

#include "fu-firmware-common.h"
#include "fu-srec-firmware.h"

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
dfu_firmware_from_srec (DfuFirmware *firmware,
			GBytes *bytes,
			DfuFirmwareParseFlags flags,
			GError **error)
{
	g_autoptr(FuFirmware) firmware_new = fu_srec_firmware_new ();
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
		dfu_firmware_add_image (firmware, image);

	}
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
