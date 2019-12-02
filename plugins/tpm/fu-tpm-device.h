/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_TPM_DEVICE (fu_tpm_device_get_type ())
G_DECLARE_FINAL_TYPE (FuTpmDevice, fu_tpm_device, FU, TPM_DEVICE, FuUdevDevice)
