/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"

#define FU_TYPE_REDFISH_DEVICE (fu_redfish_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuRedfishDevice, fu_redfish_device, FU, REDFISH_DEVICE, FuDevice)

struct _FuRedfishDeviceClass
{
	FuDeviceClass		 parent_class;
};

FuRedfishBackend *fu_redfish_device_get_backend		(FuRedfishDevice	*self);
gboolean	 fu_redfish_device_poll_task		(FuRedfishDevice	*self,
							 const gchar		*location,
							 GError			**error);
