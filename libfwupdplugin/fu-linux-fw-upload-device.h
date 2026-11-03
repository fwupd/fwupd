/*
 * Copyright 2026 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

G_BEGIN_DECLS

#define FU_TYPE_LINUX_FW_UPLOAD_DEVICE (fu_linux_fw_upload_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLinuxFwUploadDevice,
			 fu_linux_fw_upload_device,
			 FU,
			 LINUX_FW_UPLOAD_DEVICE,
			 FuUdevDevice)

struct _FuLinuxFwUploadDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_linux_fw_upload_device_cancel(FuLinuxFwUploadDevice *self,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

G_END_DECLS
