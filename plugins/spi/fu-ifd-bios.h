/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-ifd-image.h"

#define FU_TYPE_IFD_BIOS (fu_ifd_bios_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuIfdBios, fu_ifd_bios, FU, IFD_BIOS, FuIfdImage)

struct _FuIfdBiosClass
{
	FuIfdImageClass		 parent_class;
};

FuFirmware	*fu_ifd_bios_new		(void);
