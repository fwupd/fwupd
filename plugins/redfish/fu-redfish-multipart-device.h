/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_MULTIPART_DEVICE (fu_redfish_multipart_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRedfishMultipartDevice, fu_redfish_multipart_device, FU, REDFISH_MULTIPART_DEVICE, FuRedfishDevice)
