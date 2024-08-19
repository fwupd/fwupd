/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"

#define FU_TYPE_REDFISH_DEVICE (fu_redfish_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuRedfishDevice, fu_redfish_device, FU, REDFISH_DEVICE, FuDevice)

struct _FuRedfishDeviceClass {
	FuDeviceClass parent_class;
};

FuRedfishBackend *
fu_redfish_device_get_backend(FuRedfishDevice *self);
gboolean
fu_redfish_device_poll_task(FuRedfishDevice *self,
			    const gchar *location,
			    FuProgress *progress,
			    GError **error);
guint
fu_redfish_device_get_reset_pre_delay(FuRedfishDevice *self);
guint
fu_redfish_device_get_reset_post_delay(FuRedfishDevice *self);
