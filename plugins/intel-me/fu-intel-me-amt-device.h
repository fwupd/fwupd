/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_ME_AMT_DEVICE (fu_intel_me_amt_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMeAmtDevice,
		     fu_intel_me_amt_device,
		     FU,
		     INTEL_ME_AMT_DEVICE,
		     FuMeiDevice)
