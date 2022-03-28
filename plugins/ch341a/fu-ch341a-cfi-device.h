/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CH341A_CFI_DEVICE (fu_ch341a_cfi_device_get_type())
G_DECLARE_FINAL_TYPE(FuCh341aCfiDevice, fu_ch341a_cfi_device, FU, CH341A_CFI_DEVICE, FuCfiDevice)
