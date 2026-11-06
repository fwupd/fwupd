/*
 * Copyright 2026 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_PSB_DEVICE (fu_dell_psb_device_get_type())
G_DECLARE_FINAL_TYPE(FuDellPsbDevice, fu_dell_psb_device, FU, DELL_PSB_DEVICE, FuBlockDevice)
