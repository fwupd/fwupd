/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-tcg-opal-struct.h"

#define FU_TYPE_TCG_OPAL_DEVICE (fu_tcg_opal_device_get_type())
G_DECLARE_FINAL_TYPE(FuTcgOpalDevice, fu_tcg_opal_device, FU, TCG_OPAL_DEVICE, FuUdevDevice)

FuTcgOpalFlags
fu_tcg_opal_device_get_opal_flags(FuTcgOpalDevice *self);
void
fu_tcg_opal_device_set_opal_flags(FuTcgOpalDevice *self, FuTcgOpalFlags opal_flags);
