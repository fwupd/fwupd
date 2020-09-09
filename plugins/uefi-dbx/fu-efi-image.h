/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_EFI_IMAGE (fu_efi_image_get_type ())
G_DECLARE_FINAL_TYPE (FuEfiImage, fu_efi_image, FU, EFI_IMAGE, GObject)

FuEfiImage	*fu_efi_image_new		(GBytes		*data,
						 GError		**error);
const gchar	*fu_efi_image_get_checksum	(FuEfiImage	*self);
