/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IGSC_AUX_DEVICE (fu_igsc_aux_device_get_type())
G_DECLARE_FINAL_TYPE(FuIgscAuxDevice, fu_igsc_aux_device, FU, IGSC_AUX_DEVICE, FuDevice)

FuIgscAuxDevice *
fu_igsc_aux_device_new(FuContext *ctx);
