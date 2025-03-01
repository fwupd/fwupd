/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2025 Arno Dubois <arno.du@orange.fr>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_HPE_DEVICE (fu_redfish_hpe_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishHpeDevice,
		     fu_redfish_hpe_device,
		     FU,
		     REDFISH_HPE_DEVICE,
		     FuRedfishDevice)
