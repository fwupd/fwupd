/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"
#include "fu-ifd-common.h"

#define FU_TYPE_IFD_IMAGE (fu_ifd_image_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIfdImage, fu_ifd_image, FU, IFD_IMAGE, FuFirmware)

struct _FuIfdImageClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_ifd_image_new(void);
void
fu_ifd_image_set_access(FuIfdImage *self, FuIfdRegion region, FuIfdAccess access);
FuIfdAccess
fu_ifd_image_get_access(FuIfdImage *self, FuIfdRegion region);
