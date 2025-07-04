/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-tpm-device.h"

#define FU_TYPE_TPM_V2_DEVICE (fu_tpm_v2_device_get_type())
G_DECLARE_FINAL_TYPE(FuTpmV2Device, fu_tpm_v2_device, FU, TPM_V2_DEVICE, FuTpmDevice)

FuTpmDevice *
fu_tpm_v2_device_new(FuContext *ctx);
