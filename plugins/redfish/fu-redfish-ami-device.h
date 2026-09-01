/*
 * Copyright 2024 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-redfish-device.h"

#define FU_TYPE_REDFISH_AMI_DEVICE (fu_redfish_ami_device_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishAmiDevice,
		     fu_redfish_ami_device,
		     FU,
		     REDFISH_AMI_DEVICE,
		     FuRedfishDevice)

gboolean
fu_redfish_ami_device_poll_task(FuRedfishAmiDevice *self,
				const gchar *task_uri,
				FuProgress *progress,
				GError **error);
