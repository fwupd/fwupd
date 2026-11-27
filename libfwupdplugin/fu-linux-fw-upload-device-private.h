/*
 * Copyright 2026 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-fw-upload-struct.h"
#include "fu-linux-fw-upload-device.h"

G_BEGIN_DECLS

FuFwUploadStatus
fu_linux_fw_upload_device_get_status(FuLinuxFwUploadDevice *self,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_linux_fw_upload_device_get_remaining_size(FuLinuxFwUploadDevice *self,
					     guint64 *remaining_size,
					     GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);

G_END_DECLS
