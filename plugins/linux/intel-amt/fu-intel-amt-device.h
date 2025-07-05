/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_AMT_DEVICE (fu_intel_amt_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelAmtDevice, fu_intel_amt_device, FU, INTEL_AMT_DEVICE, FuMeiDevice)
