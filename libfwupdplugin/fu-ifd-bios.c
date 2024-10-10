/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuIfdBios"

#include "config.h"

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-volume.h"
#include "fu-ifd-bios.h"
#include "fu-input-stream.h"
#include "fu-mem.h"

/**
 * FuIfdBios:
 *
 * An Intel BIOS section.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuIfdBios, fu_ifd_bios, FU_TYPE_IFD_IMAGE)

#define FU_IFD_BIOS_FIT_SIGNATURE 0x5449465F

static gboolean
fu_ifd_bios_parse(FuFirmware *firmware,
		  GInputStream *stream,
		  FwupdInstallFlags flags,
		  GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;
	guint img_cnt = 0;

	/* get size */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* read each volume in order */
	while (offset < streamsz) {
		g_autoptr(FuFirmware) firmware_tmp = fu_efi_volume_new();
		g_autoptr(GError) error_local = NULL;

		/* FV */
		if (!fu_firmware_parse_stream(firmware_tmp, stream, offset, flags, &error_local)) {
			g_debug("failed to read volume @0x%x of 0x%x: %s",
				(guint)offset,
				(guint)streamsz,
				error_local->message);
			offset += 0x1000;
			continue;
		}
		fu_firmware_set_offset(firmware_tmp, offset);
		if (!fu_firmware_add_image_full(firmware, firmware_tmp, error))
			return FALSE;

		/* next! */
		offset += fu_firmware_get_size(firmware_tmp);
		img_cnt++;
	}

	/* found nothing */
	if (img_cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no EFI firmware volumes");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_ifd_bios_init(FuIfdBios *self)
{
	fu_firmware_set_alignment(FU_FIRMWARE(self), FU_FIRMWARE_ALIGNMENT_4K);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_ifd_bios_class_init(FuIfdBiosClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_ifd_bios_parse;
}

/**
 * fu_ifd_bios_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.2
 **/
FuFirmware *
fu_ifd_bios_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IFD_BIOS, NULL));
}
