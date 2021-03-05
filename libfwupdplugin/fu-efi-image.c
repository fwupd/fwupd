/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-efi-common.h"
#include "fu-efi-image.h"

G_DEFINE_TYPE (FuEfiImage, fu_efi_image, FU_TYPE_FIRMWARE_IMAGE)

static void
fu_efi_image_to_string (FuFirmwareImage *image, guint idt, GString *str)
{
	fu_common_string_append_kv (str, idt, "GUID",
				    fu_efi_guid_to_name (fu_firmware_image_get_id (image)));
}

static void
fu_efi_image_init (FuEfiImage *self)
{
}

static void
fu_efi_image_class_init (FuEfiImageClass *klass)
{
	FuFirmwareImageClass *klass_image = FU_FIRMWARE_IMAGE_CLASS (klass);
	klass_image->to_string = fu_efi_image_to_string;
}

/**
 * fu_efi_image_new:
 *
 * Creates a new #FuFirmwareImage
 *
 * Since: 1.5.8
 **/
FuFirmwareImage *
fu_efi_image_new (void)
{
	return FU_FIRMWARE_IMAGE (g_object_new (FU_TYPE_EFI_IMAGE, NULL));
}
