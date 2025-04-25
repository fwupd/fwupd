/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-intel-me-heci-device.h"

#define FU_TYPE_INTEL_ME_MCHI_DEVICE (fu_intel_me_mchi_device_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMeMchiDevice,
		     fu_intel_me_mchi_device,
		     FU,
		     INTEL_ME_MCHI_DEVICE,
		     FuIntelMeHeciDevice)
