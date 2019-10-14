/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-dfu-firmware.h"

#include "dfu-element.h"
#include "dfu-format-dfu.h"
#include "dfu-format-dfuse.h"
#include "dfu-format-raw.h"
#include "dfu-image.h"

#include "fwupd-error.h"

/**
 * dfu_firmware_detect_dfu: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_RAW
 **/
DfuFirmwareFormat
dfu_firmware_detect_dfu (GBytes *bytes)
{
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new ();
	/* check versions */
	if (!fu_firmware_parse (firmware, bytes, FWUPD_INSTALL_FLAG_NONE, NULL))
		return DFU_FIRMWARE_FORMAT_UNKNOWN;
	switch (fu_dfu_firmware_get_version (FU_DFU_FIRMWARE (firmware))) {
	case DFU_VERSION_DFU_1_0:
	case DFU_VERSION_DFU_1_1:
		return DFU_FIRMWARE_FORMAT_DFU;
	case DFU_VERSION_DFUSE:
		return DFU_FIRMWARE_FORMAT_DFUSE;
	default:
		break;
	}
	return DFU_FIRMWARE_FORMAT_UNKNOWN;
}

/**
 * dfu_firmware_from_dfu: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #FwupdInstallFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from dfu data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_dfu (DfuFirmware *firmware,
		       GBytes *bytes,
		       FwupdInstallFlags flags,
		       GError **error)
{
	g_autoptr(FuFirmware) native = fu_dfu_firmware_new ();
	g_autoptr(GBytes) contents = NULL;
	if (!fu_firmware_parse (native, bytes, flags, error))
		return FALSE;

	fu_dfu_firmware_set_vid (FU_DFU_FIRMWARE (firmware),
				 fu_dfu_firmware_get_vid (FU_DFU_FIRMWARE (native)));
	fu_dfu_firmware_set_pid (FU_DFU_FIRMWARE (firmware),
				 fu_dfu_firmware_get_pid (FU_DFU_FIRMWARE (native)));
	fu_dfu_firmware_set_release (FU_DFU_FIRMWARE (firmware),
				     fu_dfu_firmware_get_release (FU_DFU_FIRMWARE (native)));

	/* parse DfuSe prefix */
	contents = fu_firmware_get_image_default_bytes (native, error);
	if (contents == NULL)
		return FALSE;
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFUSE)
		return dfu_firmware_from_dfuse (firmware, contents, flags, error);

	/* just copy old-plain DFU file */
	return dfu_firmware_from_raw (firmware, contents, flags, error);
}

static DfuVersion
dfu_convert_version (DfuFirmwareFormat format)
{
	if (format == DFU_FIRMWARE_FORMAT_DFU)
		return DFU_VERSION_DFU_1_0;
	if (format == DFU_FIRMWARE_FORMAT_DFUSE)
		return DFU_VERSION_DFUSE;
	return DFU_VERSION_UNKNOWN;
}

static GBytes *
dfu_firmware_add_footer (DfuFirmware *firmware, GBytes *contents, GError **error)
{
	g_autoptr(FuFirmware) native = fu_dfu_firmware_new ();
	g_autoptr(FuFirmwareImage) image = fu_firmware_image_new (contents);
	fu_dfu_firmware_set_vid (FU_DFU_FIRMWARE (native),
				 fu_dfu_firmware_get_vid (FU_DFU_FIRMWARE (firmware)));
	fu_dfu_firmware_set_pid (FU_DFU_FIRMWARE (native),
				 fu_dfu_firmware_get_pid (FU_DFU_FIRMWARE (firmware)));
	fu_dfu_firmware_set_release (FU_DFU_FIRMWARE (native),
				     fu_dfu_firmware_get_release (FU_DFU_FIRMWARE (firmware)));
	fu_dfu_firmware_set_version (FU_DFU_FIRMWARE (native),
				     dfu_convert_version (dfu_firmware_get_format (firmware)));
	fu_firmware_add_image (native, image);
	return fu_firmware_write (native, error);
}
/**
 * dfu_firmware_to_dfu: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs dfu firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_dfu (DfuFirmware *firmware, GError **error)
{
	/* plain DFU */
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFU) {
		GBytes *contents;
		DfuElement *element;
		g_autoptr(DfuImage) image = NULL;
		image = DFU_IMAGE (fu_firmware_get_image_default (FU_FIRMWARE (firmware), error));
		if (image == NULL)
			return NULL;
		element = dfu_image_get_element (image, 0);
		if (element == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no firmware element data to write");
			return NULL;
		}
		contents = dfu_element_get_contents (element);
		return dfu_firmware_add_footer (firmware, contents, error);
	}

	/* DfuSe */
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFUSE) {
		g_autoptr(GBytes) contents = NULL;
		contents = dfu_firmware_to_dfuse (firmware, error);
		if (contents == NULL)
			return NULL;
		return dfu_firmware_add_footer (firmware, contents, error);
	}

	g_assert_not_reached ();
	return NULL;
}
