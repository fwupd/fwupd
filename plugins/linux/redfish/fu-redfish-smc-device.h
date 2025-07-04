/*
 * Copyright 2022 Kai Michaelis <kai.michaelis@immu.ne>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_SMC_DEVICE (fu_redfish_smc_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishSmcDevice,
		     fu_redfish_smc_device,
		     FU,
		     REDFISH_SMC_DEVICE,
		     FuRedfishDevice)

struct _FuRedfishSmcDeviceClass {
	FuRedfishDeviceClass parent_class;
};
