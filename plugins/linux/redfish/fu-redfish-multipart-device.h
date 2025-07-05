/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_MULTIPART_DEVICE (fu_redfish_multipart_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishMultipartDevice,
		     fu_redfish_multipart_device,
		     FU,
		     REDFISH_MULTIPART_DEVICE,
		     FuRedfishDevice)

void
fu_redfish_multipart_device_set_apply_time(FuRedfishMultipartDevice *self, const gchar *apply_time)
    G_GNUC_NON_NULL(1, 2);
