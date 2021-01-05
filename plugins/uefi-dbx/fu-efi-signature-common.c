/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-signature-common.h"
#include "fu-efi-signature-list.h"
#include "fu-efi-signature.h"

gboolean
fu_efi_signature_list_has_checksum (FuEfiSignatureList *siglist, const gchar *checksum)
{
	g_autoptr(FuFirmwareImage) img = NULL;
	img = fu_firmware_get_image_by_checksum (FU_FIRMWARE (siglist), checksum, NULL);
	return img != NULL;
}

gboolean
fu_efi_signature_list_inclusive (FuEfiSignatureList *outer, FuEfiSignatureList *inner)
{
	g_autoptr(GPtrArray) sigs = fu_firmware_get_images (FU_FIRMWARE (inner));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index (sigs, i);
		g_autofree gchar *checksum = NULL;
		checksum = fu_firmware_image_get_checksum (FU_FIRMWARE_IMAGE (sig),
							   G_CHECKSUM_SHA256, NULL);
		if (checksum == NULL)
			continue;
		if (!fu_efi_signature_list_has_checksum (outer, checksum))
			return FALSE;
	}
	return TRUE;
}
