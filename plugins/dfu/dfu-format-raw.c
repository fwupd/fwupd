/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "dfu-element.h"
#include "dfu-format-raw.h"
#include "dfu-image.h"

#include "fwupd-error.h"

/**
 * dfu_firmware_from_raw: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #FwupdInstallFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from raw data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_raw (DfuFirmware *firmware,
		       GBytes *bytes,
		       FwupdInstallFlags flags,
		       GError **error)
{
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(DfuImage) image = NULL;
	image = dfu_image_new ();
	element = dfu_element_new ();
	dfu_element_set_contents (element, bytes);
	dfu_image_add_element (image, element);
	fu_firmware_add_image (FU_FIRMWARE (firmware), FU_FIRMWARE_IMAGE (image));
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

	image = DFU_IMAGE (fu_firmware_get_image_default (FU_FIRMWARE (firmware), error));
	if (image == NULL)
		return NULL;
	element = dfu_image_get_element (image, 0);
	if (element == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no firmware element data to write");
		return NULL;
	}
	contents = dfu_element_get_contents (element);
	return g_bytes_ref (contents);
}
