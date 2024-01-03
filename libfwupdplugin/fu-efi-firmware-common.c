/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-firmware-common.h"
#include "fu-efi-firmware-section.h"
#include "fu-input-stream.h"
#include "fu-lzma-common.h"
#include "fu-partial-input-stream.h"

/**
 * fu_efi_firmware_parse_sections:
 * @firmware: #FuFirmware
 * @fw: data
 * @flags: flags
 * @error: (nullable): optional return location for an error
 *
 * Parses a UEFI section.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.2
 **/
gboolean
fu_efi_firmware_parse_sections(FuFirmware *firmware,
			       GInputStream *stream,
			       FwupdInstallFlags flags,
			       GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_section_new();
		g_autoptr(GInputStream) partial_stream = NULL;

		/* parse maximum payload */
		partial_stream = fu_partial_input_stream_new(stream, offset, streamsz - offset);
		if (!fu_firmware_parse_stream(img,
					      partial_stream,
					      0x0,
					      flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
					      error)) {
			g_prefix_error(error,
				       "failed to parse section of size 0x%x: ",
				       (guint)streamsz);
			return FALSE;
		}

		fu_firmware_set_offset(img, offset);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* next! */
		offset += fu_common_align_up(fu_firmware_get_size(img), FU_FIRMWARE_ALIGNMENT_4);
	}

	/* success */
	return TRUE;
}
