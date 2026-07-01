/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_FIRMWARE_SIZE_MAX_DEFAULT ((100 * FU_MB) + 1)

const GType *
fu_firmware_get_image_gtypes(FuFirmware *self, guint *n_gtypes) G_GNUC_NON_NULL(1);
