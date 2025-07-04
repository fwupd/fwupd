/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UF2_DEVICE (fu_uf2_device_get_type())

G_DECLARE_FINAL_TYPE(FuUf2Device, fu_uf2_device, FU, UF2_DEVICE, FuBlockPartition)
