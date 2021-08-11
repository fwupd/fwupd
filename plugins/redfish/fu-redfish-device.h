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

/**
 * FU_REDFISH_DEVICE_FLAG_IS_BACKUP:
 *
 * The device is the other half of a dual image firmware.
 */
#define FU_REDFISH_DEVICE_FLAG_IS_BACKUP	(1 << 0)

/**
 * FU_REDFISH_DEVICE_FLAG_UNSIGNED_BUILD:
 *
 * Use unsigned development builds.
 */
#define FU_REDFISH_DEVICE_FLAG_UNSIGNED_BUILD	(1 << 1)

/**
 * FU_REDFISH_DEVICE_FLAG_WILDCARD_TARGETS:
 *
 * Do not specify the `odata.id` in the multipart update Targets array and allow
 * the BMC to deploy the firmware onto all compatible hardware.
 *
 * To use this option the payload must contain metadata that restricts it to a
 * specific SoftwareId.
 */
#define FU_REDFISH_DEVICE_FLAG_WILDCARD_TARGETS	(1 << 2)

FuRedfishBackend *fu_redfish_device_get_backend		(FuRedfishDevice	*self);
gboolean
fu_redfish_device_poll_task(FuRedfishDevice *self,
			    const gchar *location,
			    FuProgress *progress,
			    GError **error);
