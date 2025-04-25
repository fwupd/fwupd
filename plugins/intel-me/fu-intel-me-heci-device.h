/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_ME_HECI_DEVICE (fu_intel_me_heci_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIntelMeHeciDevice,
			 fu_intel_me_heci_device,
			 FU,
			 INTEL_ME_HECI_DEVICE,
			 FuHeciDevice)

struct _FuIntelMeHeciDeviceClass {
	FuHeciDeviceClass parent_class;
};

#define FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM "leaked-km"
