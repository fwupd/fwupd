/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware-image.h"

#define FU_TYPE_EFI_IMAGE (fu_efi_image_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuEfiImage, fu_efi_image, FU, EFI_IMAGE, FuFirmwareImage)

struct _FuEfiImageClass
{
	FuFirmwareImageClass	 parent_class;
};

FuFirmwareImage	*fu_efi_image_new		(void);
