/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_LEGACY_DEVICE (fu_redfish_legacy_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishLegacyDevice,
		     fu_redfish_legacy_device,
		     FU,
		     REDFISH_LEGACY_DEVICE,
		     FuRedfishDevice)
