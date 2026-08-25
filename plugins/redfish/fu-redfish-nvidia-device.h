/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
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

typedef enum {
	FU_REDFISH_NVIDIA_TASK_RUNNING,
	FU_REDFISH_NVIDIA_TASK_COMPLETED,
} FuRedfishNvidiaTaskState;

typedef enum {
	FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK,
	FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED,
	FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT,
	FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL,
} FuRedfishNvidiaTaskResponse;

FuRedfishNvidiaTaskResponse
fu_redfish_nvidia_device_classify_task_response(glong status_code,
						gboolean request_succeeded,
						gboolean has_json,
						gboolean allow_empty_200);
gboolean
fu_redfish_nvidia_device_parse_task(const gchar *task_uri,
				    FwupdJsonObject *json_task,
				    FuRedfishNvidiaTaskState *task_state,
				    guint *percentage,
				    GError **error) G_GNUC_WARN_UNUSED_RESULT;
