/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-device.h"

#define FU_TYPE_UEFI_COD_DEVICE (fu_uefi_cod_device_get_type())
G_DECLARE_FINAL_TYPE(FuUefiCodDevice, fu_uefi_cod_device, FU, UEFI_COD_DEVICE, FuUefiDevice)
