/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELAN_TS_DEVICE (fu_elan_ts_device_get_type())

G_DECLARE_FINAL_TYPE(FuElanTsDevice, fu_elan_ts_device, FU, ELAN_TS_DEVICE, FuHidrawDevice)
