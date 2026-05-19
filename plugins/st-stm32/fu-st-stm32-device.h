/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ST_STM32_DEVICE (fu_st_stm32_device_get_type())
G_DECLARE_FINAL_TYPE(FuStStm32Device, fu_st_stm32_device, FU, ST_STM32_DEVICE, FuUsbDevice)
