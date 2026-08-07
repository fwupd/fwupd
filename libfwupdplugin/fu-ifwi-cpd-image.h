/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

G_BEGIN_DECLS

#define FU_TYPE_IFWI_CPD_IMAGE (fu_ifwi_cpd_image_get_type())
G_DECLARE_FINAL_TYPE(FuIfwiCpdImage, fu_ifwi_cpd_image, FU, IFWI_CPD_IMAGE, FuFirmware)

FuFirmware *
fu_ifwi_cpd_image_new(void);

G_END_DECLS
