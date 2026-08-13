/*
 * Copyright 2024 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_NVIDIA_DEVICE (fu_redfish_nvidia_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishNvidiaDevice,
		     fu_redfish_nvidia_device,
		     FU,
		     REDFISH_NVIDIA_DEVICE,
		     FuRedfishDevice)

gboolean
fu_redfish_nvidia_device_poll_task(FuRedfishNvidiaDevice *self,
				   const gchar *task_uri,
				   FuProgress *progress,
				   GError **error);
