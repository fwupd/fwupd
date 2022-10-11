/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-intel-me-heci-device.h"

#define FU_TYPE_INTEL_ME_MCA_DEVICE (fu_intel_me_mca_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMeMcaDevice,
		     fu_intel_me_mca_device,
		     FU,
		     INTEL_ME_MCA_DEVICE,
		     FuIntelMeHeciDevice)
