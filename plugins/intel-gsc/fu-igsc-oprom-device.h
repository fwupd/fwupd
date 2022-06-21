/*
 * Copyright (C) 2022 Intel, Inc.
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-igsc-heci.h"

#define FU_TYPE_IGSC_OPROM_DEVICE (fu_igsc_oprom_device_get_type())
G_DECLARE_FINAL_TYPE(FuIgscOpromDevice, fu_igsc_oprom_device, FU, IGSC_OPROM_DEVICE, FuDevice)

FuIgscOpromDevice *
fu_igsc_oprom_device_new(FuContext *ctx, enum gsc_fwu_heci_payload_type payload_type);
