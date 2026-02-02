/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include "fu-novatek-ts-plugin.h"

#define FU_TYPE_NOVATEK_TS_DEVICE (fu_novatek_ts_device_get_type())
G_DECLARE_FINAL_TYPE(FuNovatekTsDevice, fu_novatek_ts_device, FU, NOVATEK_TS_DEVICE, FuHidrawDevice)
