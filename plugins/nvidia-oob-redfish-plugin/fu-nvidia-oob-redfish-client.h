/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * thin wrapper around libcurl implementing the DMTF Redfish host
 * interface (DSP0270) subset needed for OOB firmware updates:
 *
 *   - GET /redfish/v1/
 *   - GET /redfish/v1/UpdateService
 *   - GET /redfish/v1/UpdateService/FirmwareInventory
 *   - POST multipart/form-data to UpdateService -> TaskMonitor
 *   - GET /redfish/v1/TaskService/Tasks/{id}
 *
 * the client resolves the BMC IP from the NVIDIA_OOB_BMC_HOST env var,
 * from BmcHost in /etc/fwupd/nvidia_oob.conf, or via link-local fallback
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_NVIDIA_OOB_REDFISH_CLIENT (fu_nvidia_oob_redfish_client_get_type())
G_DECLARE_FINAL_TYPE(FuNvidiaOobRedfishClient,
		     fu_nvidia_oob_redfish_client,
		     FU,
		     NVIDIA_OOB_REDFISH_CLIENT,
		     GObject)

typedef enum {
	FU_OOB_TASK_STATE_UNKNOWN,
	FU_OOB_TASK_STATE_NEW,
	FU_OOB_TASK_STATE_RUNNING,
	FU_OOB_TASK_STATE_COMPLETED,
	FU_OOB_TASK_STATE_EXCEPTION,
	FU_OOB_TASK_STATE_CANCELLED,
} FuOobTaskState;

FuNvidiaOobRedfishClient *
fu_nvidia_oob_redfish_client_new(void);

void
fu_nvidia_oob_redfish_client_add_device(FuNvidiaOobRedfishClient *self, FuDevice *device)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_nvidia_oob_redfish_client_get_devices(FuNvidiaOobRedfishClient *self) G_GNUC_NON_NULL(1);

gboolean
fu_nvidia_oob_redfish_client_setup(FuNvidiaOobRedfishClient *self, GError **error);

/* GET -- caller owns the returned FwupdJsonNode */
FwupdJsonNode *
fu_nvidia_oob_redfish_client_get(FuNvidiaOobRedfishClient *self, const gchar *uri, GError **error);

/* enumerate the members of UpdateService/FirmwareInventory;
 * returns a GPtrArray of newly-allocated Redfish URIs (gchar *)
 */
GPtrArray *
fu_nvidia_oob_redfish_client_list_inventory(FuNvidiaOobRedfishClient *self, GError **error);

/* upload a CAB-extracted firmware image using MultipartHttpPush;
 * returns the TaskMonitor URI from the Location header on success;
 * caller is responsible for polling that URI with poll_task()
 */
gchar *
fu_nvidia_oob_redfish_client_multipart_push(FuNvidiaOobRedfishClient *self,
					    const gchar *target_uri,
					    GBytes *firmware_blob,
					    GError **error);

/* poll a TaskMonitor URI once; out_percent is populated with the
 * PercentComplete field when present (0..100)
 *
 * task_uri_inout points to the URI the caller originally got from the
 * Location: header of the multipart POST (typically a TaskMonitor URI);
 * if the BMC reaps the Monitor sub-resource after task completion (404),
 * this function transparently falls back to the persistent Task resource
 * at /Tasks/<id> and updates *task_uri_inout in place so subsequent polls
 * go directly to the Task; the caller owns *task_uri_inout in both cases
 * and must g_free() (or autofree) it
 */
FuOobTaskState
fu_nvidia_oob_redfish_client_poll_task(FuNvidiaOobRedfishClient *self,
				       gchar **task_uri_inout,
				       guint *out_percent,
				       gchar **out_message,
				       GError **error);
