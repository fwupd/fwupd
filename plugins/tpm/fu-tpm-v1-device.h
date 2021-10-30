/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-tpm-device.h"

#define FU_TYPE_TPM_V1_DEVICE (fu_tpm_v1_device_get_type())
G_DECLARE_FINAL_TYPE(FuTpmV1Device, fu_tpm_v1_device, FU, TPM_V1_DEVICE, FuTpmDevice)

FuTpmDevice *
fu_tpm_v1_device_new(FuContext *ctx);
