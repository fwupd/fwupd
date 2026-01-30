/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include "fu-nvt-ts-plugin.h"

#define FU_TYPE_NVT_TS_DEVICE (fu_nvt_ts_device_get_type())
G_DECLARE_FINAL_TYPE(FuNvtTsDevice, fu_nvt_ts_device, FU, NVT_TS_DEVICE, FuHidrawDevice)

#define FU_NVT_TS_DEVICE_FLAG_SKIP_STATUS_REGISTER_CONTROL "skip-status-register-control"
