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
#include "fu-lzma-common.h"

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
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	gsize offset = 0;
	gsize bufsz = g_bytes_get_size(fw);

	while (offset < bufsz) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_section_new();
		g_autoptr(GBytes) blob = NULL;

		/* maximum payload */
		blob = fu_bytes_new_offset(fw, offset, bufsz - offset, error);
		if (blob == NULL)
			return FALSE;

		/* parse section */
		if (!fu_firmware_parse(img, blob, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error))
			return FALSE;
		fu_firmware_set_offset(img, offset);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* next! */
		offset += fu_firmware_get_size(img);
	}
	if (offset != g_bytes_get_size(fw)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "EFI sections overflow 0x%x of 0x%x",
			    (guint)offset,
			    (guint)g_bytes_get_size(fw));
		return FALSE;
	}

	/* success */
	return TRUE;
}
