/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"

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
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(DfuImage) image = NULL;
	image = dfu_image_new ();
	chk = fu_chunk_bytes_new (bytes);
	dfu_image_add_chunk (image, chk);
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
	FuChunk *chk;
	DfuImage *image;

	image = DFU_IMAGE (fu_firmware_get_image_default (FU_FIRMWARE (firmware), error));
	if (image == NULL)
		return NULL;
	chk = dfu_image_get_chunk_by_idx (image, 0);
	if (chk == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no firmware chunk data to write");
		return NULL;
	}
	return fu_chunk_get_bytes (chk);
}
