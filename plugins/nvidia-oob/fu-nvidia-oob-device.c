/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>
#include <unistd.h>

#include "fu-nvidia-oob-device.h"

/* 1800 polls at 2s covers the worst-case bundle flash of ~60 minutes */
#define FU_NVIDIA_OOB_TASK_POLL_DELAY 2000u /* ms */
#define FU_NVIDIA_OOB_TASK_POLL_CNT   1800u

/* a newly accepted upload needs a few seconds before /Tasks/<id> appears */
#define FU_NVIDIA_OOB_TASK_CREATION_GRACE 5 /* polls */

/* consecutive transient failures tolerated before the update is abandoned */
#define FU_NVIDIA_OOB_TASK_ERRORS_MAX 5

/* only used when the BMC UpdateService does not report MaxImageSizeBytes */
#define FU_NVIDIA_OOB_FIRMWARE_SIZE_MAX (256 * FU_MB)

/* the chassis carrying the OEM aux-rail reset */
#define FU_NVIDIA_OOB_CHASSIS_URI "/redfish/v1/Chassis/BMC_0"

struct _FuNvidiaOobDevice {
	FuDevice parent_instance;
	FuNvidiaOobBmc *bmc;
	FwupdJsonObject *json_member;
};

G_DEFINE_TYPE(FuNvidiaOobDevice, fu_nvidia_oob_device, FU_TYPE_DEVICE)

typedef enum {
	FU_NVIDIA_OOB_TASK_RUNNING,
	FU_NVIDIA_OOB_TASK_COMPLETED,
} FuNvidiaOobTaskState;

typedef enum {
	FU_NVIDIA_OOB_TASK_RESPONSE_TASK,
	FU_NVIDIA_OOB_TASK_RESPONSE_REAPED,
	FU_NVIDIA_OOB_TASK_RESPONSE_TRANSIENT,
	FU_NVIDIA_OOB_TASK_RESPONSE_FATAL,
} FuNvidiaOobTaskResponse;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

typedef struct {
	FuNvidiaOobBmc *bmc;
	FuProgress *progress;
	const gchar *task_uri;
	const gchar *monitor_uri;
	guint attempt;
	guint consecutive_errors;
	/* set once the persistent /Tasks/<id> URI has returned a valid TaskState, to
	 * tell "task reaped after completing" apart from "task never existed" */
	gboolean saw_persistent_task;
} FuNvidiaOobTaskHelper;

static void
fu_nvidia_oob_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);
	if (self->json_member != NULL) {
		fwupd_codec_string_append(
		    str,
		    idt,
		    "InventoryUri",
		    fwupd_json_object_get_string(self->json_member, "@odata.id", NULL));
	}
}

static gchar *
fu_nvidia_oob_device_derive_task_uri(const gchar *monitor_uri)
{
	const gchar *suffix = "/Monitor";
	if (!g_str_has_suffix(monitor_uri, suffix))
		return NULL;
	return g_strndup(monitor_uri, strlen(monitor_uri) - strlen(suffix));
}

static FuNvidiaOobTaskResponse
fu_nvidia_oob_device_classify_task_response(glong status_code,
					    gboolean request_succeeded,
					    gboolean has_json,
					    gboolean allow_empty_200)
{
	/* a reap status wins over any body the BMC happens to return */
	if (status_code == 404)
		return FU_NVIDIA_OOB_TASK_RESPONSE_REAPED;

	/* these statuses can clear while an update remains active */
	if (status_code == 0 || status_code == 408 || status_code == 425 || status_code == 429 ||
	    status_code >= 500)
		return FU_NVIDIA_OOB_TASK_RESPONSE_TRANSIENT;

	/* never let task-shaped JSON turn an HTTP error into success */
	if (status_code < 200 || status_code >= 300)
		return FU_NVIDIA_OOB_TASK_RESPONSE_FATAL;

	/* a 2xx response that failed transport or JSON validation is not a task */
	if (!request_succeeded)
		return FU_NVIDIA_OOB_TASK_RESPONSE_FATAL;

	if (!has_json) {
		/* once the task is done the task monitor answers 200 with an empty body */
		if (status_code == 200 && allow_empty_200)
			return FU_NVIDIA_OOB_TASK_RESPONSE_REAPED;
		return FU_NVIDIA_OOB_TASK_RESPONSE_TRANSIENT;
	}

	return FU_NVIDIA_OOB_TASK_RESPONSE_TASK;
}

static gboolean
fu_nvidia_oob_device_task_state_is_running(const gchar *state)
{
	if (state == NULL)
		return FALSE;

	/* the DMTF state of a task being cancelled */
	if (g_str_has_prefix(state, "Cancel") && g_str_has_suffix(state, "ing"))
		return TRUE;
	return g_strcmp0(state, "New") == 0 || g_strcmp0(state, "Starting") == 0 ||
	       g_strcmp0(state, "Running") == 0 || g_strcmp0(state, "Suspended") == 0 ||
	       g_strcmp0(state, "Pending") == 0 || g_strcmp0(state, "Stopping") == 0 ||
	       g_strcmp0(state, "Service") == 0 || g_strcmp0(state, "Continue") == 0;
}

static gboolean
fu_nvidia_oob_device_parse_task(const gchar *task_uri,
				FwupdJsonObject *json_task,
				FuNvidiaOobTaskState *task_state,
				guint *percentage,
				GError **error)
{
	const gchar *state;
	const gchar *status;
	gint64 percent = 0;

	g_return_val_if_fail(task_uri != NULL, FALSE);
	g_return_val_if_fail(json_task != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	state = fwupd_json_object_get_string(json_task, "TaskState", NULL);
	status = fwupd_json_object_get_string(json_task, "TaskStatus", NULL);
	(void)fwupd_json_object_get_integer_with_default(json_task,
							 "PercentComplete",
							 &percent,
							 0,
							 NULL);
	if (percentage != NULL)
		*percentage = (guint)CLAMP(percent, 0, 100);

	if (state == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "BMC task %s has no TaskState",
			    task_uri);
		return FALSE;
	}

	/* per DMTF DSP0266 the BMC sets TaskStatus=Critical once the operation has
	 * failed, even while TaskState still shows Running */
	if (g_strcmp0(status, "Critical") == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "BMC task %s has TaskStatus=Critical (TaskState=%s)",
			    task_uri,
			    state);
		return FALSE;
	}

	/* terminal failure states, where the task will make no further progress */
	if (g_strcmp0(state, "Exception") == 0 || g_strcmp0(state, "Killed") == 0 ||
	    g_strcmp0(state, "Cancelled") == 0 || g_strcmp0(state, "UserIntervention") == 0 ||
	    g_strcmp0(state, "Interrupted") == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "BMC task %s failed (TaskState=%s)",
			    task_uri,
			    state);
		return FALSE;
	}

	if (g_strcmp0(state, "Completed") == 0) {
		if (task_state != NULL)
			*task_state = FU_NVIDIA_OOB_TASK_COMPLETED;
		return TRUE;
	}

	if (!fu_nvidia_oob_device_task_state_is_running(state)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "BMC task %s has unknown TaskState=%s",
			    task_uri,
			    state);
		return FALSE;
	}

	/* some BMC versions report 100% before leaving a known running state */
	if (task_state != NULL) {
		*task_state = percent == 100 ? FU_NVIDIA_OOB_TASK_COMPLETED
					     : FU_NVIDIA_OOB_TASK_RUNNING;
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_poll_request(FuNvidiaOobBmc *bmc,
				  const gchar *uri,
				  gboolean allow_empty_200,
				  FuNvidiaOobTaskResponse *response,
				  FwupdJsonObject **json_task,
				  GError **error)
{
	gboolean request_succeeded;
	glong status_code;
	g_autoptr(FuNvidiaOobRequest) request = fu_nvidia_oob_bmc_request_new(bmc);
	g_autoptr(FwupdJsonObject) json_tmp = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(response != NULL, FALSE);
	g_return_val_if_fail(json_task != NULL, FALSE);
	g_return_val_if_fail(*json_task == NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	request_succeeded = fu_nvidia_oob_request_perform(request, uri, &error_local);
	status_code = fu_nvidia_oob_request_get_status_code(request);
	json_tmp = fu_nvidia_oob_request_get_json_object(request, NULL);
	*response = fu_nvidia_oob_device_classify_task_response(status_code,
								request_succeeded,
								json_tmp != NULL,
								allow_empty_200);

	if (*response == FU_NVIDIA_OOB_TASK_RESPONSE_TASK) {
		*json_task = g_steal_pointer(&json_tmp);
		return TRUE;
	}
	if (*response == FU_NVIDIA_OOB_TASK_RESPONSE_REAPED)
		return TRUE;
	if (*response == FU_NVIDIA_OOB_TASK_RESPONSE_TRANSIENT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "transient HTTP %li polling task %s: %s",
			    status_code,
			    uri,
			    error_local != NULL ? error_local->message : "no error detail");
		return FALSE;
	}
	if (status_code == 401 || status_code == 403) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "authentication failed with HTTP %li polling task %s",
			    status_code,
			    uri);
		return FALSE;
	}
	if (error_local != NULL) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "unexpected HTTP %li polling task %s",
		    status_code,
		    uri);
	return FALSE;
}

static gboolean
fu_nvidia_oob_device_poll_error(FuNvidiaOobTaskHelper *helper, GError *error_local, GError **error)
{
	g_autoptr(GError) error_taken = error_local;

	if (!g_error_matches(error_taken, FWUPD_ERROR, FWUPD_ERROR_BUSY)) {
		g_propagate_error(error, g_steal_pointer(&error_taken));
		return FALSE;
	}
	if (++helper->consecutive_errors > FU_NVIDIA_OOB_TASK_ERRORS_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "gave up after %u consecutive poll errors: %s",
			    helper->consecutive_errors,
			    error_taken->message);
		return FALSE;
	}
	g_propagate_error(error, g_steal_pointer(&error_taken));
	return FALSE;
}

static gboolean
fu_nvidia_oob_device_poll_task_reaped(FuNvidiaOobTaskHelper *helper, GError **error)
{
	FuNvidiaOobTaskResponse response;
	FuNvidiaOobTaskState task_state;
	guint percentage = 0;
	g_autoptr(FwupdJsonObject) json_task = NULL;
	g_autoptr(GError) error_local = NULL;

	/* the persistent task is gone, so validate the monitor before declaring
	 * success, as a live monitor can still report a terminal failure */
	if (helper->monitor_uri != NULL && !g_str_equal(helper->task_uri, helper->monitor_uri)) {
		if (!fu_nvidia_oob_device_poll_request(helper->bmc,
						       helper->monitor_uri,
						       TRUE,
						       &response,
						       &json_task,
						       &error_local)) {
			return fu_nvidia_oob_device_poll_error(helper,
							       g_steal_pointer(&error_local),
							       error);
		}
		if (response == FU_NVIDIA_OOB_TASK_RESPONSE_TASK) {
			if (!fu_nvidia_oob_device_parse_task(helper->monitor_uri,
							     json_task,
							     &task_state,
							     &percentage,
							     error))
				return FALSE;
			fu_progress_set_percentage(helper->progress, percentage);
			if (task_state == FU_NVIDIA_OOB_TASK_COMPLETED) {
				fu_progress_set_percentage(helper->progress, 100);
				return TRUE;
			}
			helper->consecutive_errors = 0;
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_BUSY,
					    "monitor is active, waiting for the persistent task");
			return FALSE;
		}
	}

	/* both resources are gone after at least one valid poll of the persistent
	 * task, so the BMC has finished with them */
	if (helper->saw_persistent_task) {
		g_debug("task and monitor reaped after a valid poll, treating as completed");
		fu_progress_set_percentage(helper->progress, 100);
		return TRUE;
	}

	if (helper->attempt <= FU_NVIDIA_OOB_TASK_CREATION_GRACE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "task is not yet visible on poll %u",
			    helper->attempt);
		return FALSE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "task not found after the grace period; the upload may not have "
			    "started, or the task was stale");
	return FALSE;
}

static gboolean
fu_nvidia_oob_device_poll_task_once(FuNvidiaOobTaskHelper *helper, GError **error)
{
	FuNvidiaOobTaskResponse response;
	FuNvidiaOobTaskState task_state;
	gboolean is_monitor = g_str_has_suffix(helper->task_uri, "/Monitor");
	guint percentage = 0;
	g_autoptr(FwupdJsonObject) json_task = NULL;
	g_autoptr(GError) error_local = NULL;

	if (!fu_nvidia_oob_device_poll_request(helper->bmc,
					       helper->task_uri,
					       is_monitor,
					       &response,
					       &json_task,
					       &error_local)) {
		return fu_nvidia_oob_device_poll_error(helper,
						       g_steal_pointer(&error_local),
						       error);
	}
	if (response == FU_NVIDIA_OOB_TASK_RESPONSE_REAPED)
		return fu_nvidia_oob_device_poll_task_reaped(helper, error);

	if (!fu_nvidia_oob_device_parse_task(helper->task_uri,
					     json_task,
					     &task_state,
					     &percentage,
					     error))
		return FALSE;
	fu_progress_set_percentage(helper->progress, percentage);
	if (!is_monitor)
		helper->saw_persistent_task = TRUE;
	helper->consecutive_errors = 0;

	/* log progress every 60 seconds */
	if (helper->attempt % 30 == 1) {
		g_debug("task poll #%u, %u%% complete (~%u s elapsed)",
			helper->attempt,
			percentage,
			helper->attempt * FU_NVIDIA_OOB_TASK_POLL_DELAY / 1000);
	}

	if (task_state == FU_NVIDIA_OOB_TASK_COMPLETED) {
		g_debug("task completed at %u%%", percentage);
		fu_progress_set_percentage(helper->progress, 100);
		return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "task %s is %u%% complete",
		    helper->task_uri,
		    percentage);
	return FALSE;
}

static gboolean
fu_nvidia_oob_device_poll_task_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNvidiaOobTaskHelper *helper = (FuNvidiaOobTaskHelper *)user_data;
	g_autoptr(GError) error_local = NULL;

	helper->attempt++;
	if (fu_nvidia_oob_device_poll_task_once(helper, &error_local))
		return TRUE;

	/* fu_device_retry_full() retries any error without a recovery registered in
	 * _init(), so normalize everything the poll does not want retried into
	 * FWUPD_ERROR_WRITE, keeping the original message */
	if (error_local != NULL && !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BUSY) &&
	    !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, error_local->message);
		return FALSE;
	}
	g_propagate_error(error, g_steal_pointer(&error_local));
	return FALSE;
}

static gboolean
fu_nvidia_oob_device_poll_task(FuNvidiaOobDevice *self,
			       const gchar *location,
			       FuProgress *progress,
			       GError **error)
{
	FuNvidiaOobTaskHelper helper = {
	    .bmc = self->bmc,
	    .progress = progress,
	    .monitor_uri = location,
	};
	/* PercentComplete is only updated on the persistent /Tasks/<id> resource, so
	 * poll that, keeping the monitor to detect its reap separately */
	g_autofree gchar *persistent_uri = fu_nvidia_oob_device_derive_task_uri(location);

	helper.task_uri = persistent_uri != NULL ? persistent_uri : location;
	g_debug("polling task %s for up to %u s",
		helper.task_uri,
		FU_NVIDIA_OOB_TASK_POLL_CNT * FU_NVIDIA_OOB_TASK_POLL_DELAY / 1000);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nvidia_oob_device_poll_task_cb,
				  FU_NVIDIA_OOB_TASK_POLL_CNT,
				  FU_NVIDIA_OOB_TASK_POLL_DELAY,
				  &helper,
				  error)) {
		g_prefix_error(error, "failed to poll BMC task %s: ", helper.task_uri);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_slot_is_pending(FwupdJsonObject *json_member)
{
	const gchar *state;
	const gchar *version;
	g_autoptr(FwupdJsonObject) json_nvidia = NULL;
	g_autoptr(FwupdJsonObject) json_oem = NULL;
	g_autoptr(FwupdJsonObject) json_slot = NULL;

	json_oem = fwupd_json_object_get_object(json_member, "Oem", NULL);
	if (json_oem == NULL)
		return FALSE;
	json_nvidia = fwupd_json_object_get_object(json_oem, "Nvidia", NULL);
	if (json_nvidia == NULL)
		return FALSE;
	json_slot = fwupd_json_object_get_object(json_nvidia, "ActiveFirmwareSlot", NULL);
	if (json_slot == NULL)
		return FALSE;
	state = fwupd_json_object_get_string(json_slot, "FirmwareState", NULL);
	if (g_strcmp0(state, "PendingActivation") != 0)
		return FALSE;

	/* an empty version means the slot was erased, so nothing is staged */
	version = fwupd_json_object_get_string(json_slot, "Version", NULL);
	return version != NULL && version[0] != '\0';
}

static void
fu_nvidia_oob_device_set_explicit_only(FuNvidiaOobDevice *self)
{
	FuDevice *device = FU_DEVICE(self);

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
		return;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES);
}

static void
fu_nvidia_oob_device_set_staged(FuNvidiaOobDevice *self)
{
	GPtrArray *devices = fu_nvidia_oob_bmc_get_devices(self->bmc);

	fu_nvidia_oob_device_set_explicit_only(self);
	for (guint i = 0; i < devices->len; i++)
		fu_nvidia_oob_device_set_explicit_only(
		    FU_NVIDIA_OOB_DEVICE(g_ptr_array_index(devices, i)));
}

static void
fu_nvidia_oob_device_probe_staged(FuNvidiaOobDevice *self)
{
	GPtrArray *devices = fu_nvidia_oob_bmc_get_devices(self->bmc);
	gboolean staged = fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);

	for (guint i = 0; i < devices->len && !staged; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION))
			staged = TRUE;
	}
	if (staged)
		fu_nvidia_oob_device_set_staged(self);
}

static gboolean
fu_nvidia_oob_device_get_json(FuNvidiaOobDevice *self,
			      const gchar *uri,
			      FwupdJsonObject **json_obj,
			      GError **error)
{
	g_autoptr(FuNvidiaOobRequest) request = fu_nvidia_oob_bmc_request_new(self->bmc);

	if (!fu_nvidia_oob_request_perform(request, uri, error))
		return FALSE;
	*json_obj = fu_nvidia_oob_request_get_json_object(request, error);
	return *json_obj != NULL;
}

static gboolean
fu_nvidia_oob_device_probe_pcie_function(FuNvidiaOobDevice *self, const gchar *uri, GError **error)
{
	FuDevice *device = FU_DEVICE(self);
	const gchar *tmp;
	guint64 vendor_id = 0x0;
	guint64 model_id = 0x0;
	guint64 subsystem_vendor_id = 0x0;
	guint64 subsystem_model_id = 0x0;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	if (!fu_nvidia_oob_device_get_json(self, uri, &json_obj, error))
		return FALSE;
	tmp = fwupd_json_object_get_string(json_obj, "VendorId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp, &vendor_id, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "DeviceId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp, &model_id, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "SubsystemVendorId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp,
				 &subsystem_vendor_id,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "SubsystemId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp,
				 &subsystem_model_id,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}
	if (vendor_id != 0x0)
		fu_device_add_instance_u16(device, "VEN", vendor_id);
	if (model_id != 0x0)
		fu_device_add_instance_u16(device, "DEV", model_id);
	if (subsystem_vendor_id != 0x0 && subsystem_model_id != 0x0) {
		g_autofree gchar *subsys = g_strdup_printf("%04X%04X",
							   (guint)subsystem_vendor_id,
							   (guint)subsystem_model_id);
		fu_device_add_instance_str(device, "SUBSYS", subsys);
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_probe_pcie_functions(FuNvidiaOobDevice *self, const gchar *uri, GError **error)
{
	g_autoptr(FwupdJsonArray) json_members = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	if (!fu_nvidia_oob_device_get_json(self, uri, &json_obj, error))
		return FALSE;
	json_members = fwupd_json_object_get_array(json_obj, "Members", NULL);
	if (json_members == NULL)
		return TRUE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_members); i++) {
		const gchar *id;
		g_autoptr(FwupdJsonObject) json_member = NULL;

		json_member = fwupd_json_array_get_object(json_members, i, error);
		if (json_member == NULL)
			return FALSE;
		id = fwupd_json_object_get_string(json_member, "@odata.id", NULL);
		if (id != NULL && !fu_nvidia_oob_device_probe_pcie_function(self, id, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_probe_pcie_devices(FuNvidiaOobDevice *self, const gchar *uri, GError **error)
{
	g_autoptr(FwupdJsonArray) json_members = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	if (!fu_nvidia_oob_device_get_json(self, uri, &json_obj, error))
		return FALSE;
	json_members = fwupd_json_object_get_array(json_obj, "Members", NULL);
	if (json_members == NULL)
		return TRUE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_members); i++) {
		const gchar *id;
		g_autoptr(FwupdJsonObject) json_member = NULL;
		g_autoptr(FwupdJsonObject) json_pcie_device = NULL;
		g_autoptr(FwupdJsonObject) json_functions = NULL;

		json_member = fwupd_json_array_get_object(json_members, i, error);
		if (json_member == NULL)
			return FALSE;
		id = fwupd_json_object_get_string(json_member, "@odata.id", NULL);
		if (id == NULL)
			continue;
		if (!fu_nvidia_oob_device_get_json(self, id, &json_pcie_device, error))
			return FALSE;
		json_functions =
		    fwupd_json_object_get_object(json_pcie_device, "PCIeFunctions", NULL);
		if (json_functions == NULL)
			continue;
		id = fwupd_json_object_get_string(json_functions, "@odata.id", NULL);
		if (id != NULL && !fu_nvidia_oob_device_probe_pcie_functions(self, id, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_probe_related_item(FuNvidiaOobDevice *self, const gchar *uri, GError **error)
{
	const gchar *id;
	g_autoptr(FwupdJsonObject) json_devices = NULL;
	g_autoptr(FwupdJsonObject) json_functions = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	if (!fu_nvidia_oob_device_get_json(self, uri, &json_obj, error))
		return FALSE;

	/* a PCIe device lists its functions */
	json_functions = fwupd_json_object_get_object(json_obj, "PCIeFunctions", NULL);
	if (json_functions != NULL) {
		id = fwupd_json_object_get_string(json_functions, "@odata.id", NULL);
		if (id != NULL && !fu_nvidia_oob_device_probe_pcie_functions(self, id, error))
			return FALSE;
	}

	/* a component's inventory entry relates to its chassis, which lists the PCIe
	 * devices whose IDs identify the board variant */
	json_devices = fwupd_json_object_get_object(json_obj, "PCIeDevices", NULL);
	if (json_devices != NULL) {
		id = fwupd_json_object_get_string(json_devices, "@odata.id", NULL);
		if (id != NULL && !fu_nvidia_oob_device_probe_pcie_devices(self, id, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_probe(FuDevice *device, GError **error)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);
	const gchar *inventory_uri;
	const gchar *manufacturer;
	const gchar *tmp;
	gboolean updateable = TRUE;
	guint64 size_max = fu_nvidia_oob_bmc_get_max_image_size(self->bmc);
	g_autofree gchar *basename = NULL;
	g_autoptr(FwupdJsonArray) json_related = NULL;

	if (self->json_member == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no inventory member");
		return FALSE;
	}

	/* the same physical and logical IDs as the redfish plugin used, so device IDs
	 * and update history carry over */
	inventory_uri = fwupd_json_object_get_string(self->json_member, "@odata.id", error);
	if (inventory_uri == NULL)
		return FALSE;
	fu_device_set_physical_id(device, "Redfish-Inventory");
	fu_device_set_logical_id(device, inventory_uri);

	/* the BMC names every entry "Software Inventory", so name each component after
	 * its inventory URI; fu_device_set_name() renders the underscores as spaces */
	basename = g_path_get_basename(inventory_uri);
	if (basename != NULL && basename[0] != '\0' && g_strcmp0(basename, ".") != 0 &&
	    g_strcmp0(basename, "/") != 0)
		fu_device_set_name(device, basename);

	/* the inventory Id is stable and unique per entry, so it is the match key for
	 * a release targeting one component; it carries no Manufacturer, which differs
	 * between OEM-branded and NVIDIA-branded images on the same system */
	tmp = fwupd_json_object_get_string(self->json_member, "Id", NULL);
	fu_device_set_backend_id(device, tmp != NULL ? tmp : basename);
	fu_device_add_instance_str(device, "ID", fu_device_get_backend_id(device));
	if (!fu_device_build_instance_id(device, error, "REDFISH", "ID", NULL))
		return FALSE;

	tmp = fwupd_json_object_get_string(self->json_member, "Manufacturer", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_device_set_vendor(device, tmp);
	tmp = fwupd_json_object_get_string(self->json_member, "Version", NULL);
	if (tmp != NULL && tmp[0] != '\0' && g_strcmp0(tmp, "-*") != 0) {
		fu_device_set_version(device, tmp);
		fu_device_set_version_format(device, fu_version_guess_format(tmp));
	}
	if (!fwupd_json_object_get_boolean_with_default(self->json_member,
							"Updateable",
							&updateable,
							TRUE,
							error))
		return FALSE;
	if (updateable)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* staged by the BMC and waiting for the aux power cycle */
	if (fu_nvidia_oob_device_slot_is_pending(self->json_member))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_nvidia_oob_device_probe_staged(self);

	/* a component that comes in board variants, such as the GPU, takes a different
	 * image on each; the PCI IDs of its board tell them apart, as they do for the
	 * BMC choosing the image from the bundle */
	json_related = fwupd_json_object_get_array(self->json_member, "RelatedItem", NULL);
	if (updateable && json_related != NULL) {
		for (guint i = 0; i < fwupd_json_array_get_size(json_related); i++) {
			g_autoptr(FwupdJsonObject) json_item = NULL;

			json_item = fwupd_json_array_get_object(json_related, i, error);
			if (json_item == NULL)
				return FALSE;
			tmp = fwupd_json_object_get_string(json_item, "@odata.id", NULL);
			if (tmp != NULL &&
			    !fu_nvidia_oob_device_probe_related_item(self, tmp, error))
				return FALSE;
		}
	}
	fu_device_build_instance_id(device, NULL, "REDFISH", "ID", "VEN", "DEV", "SUBSYS", NULL);

	/* the brand of the system, so an OEM-branded system only takes releases
	 * published by that OEM, whatever vendor the BMC reports for a component */
	manufacturer =
	    fu_context_get_hwid_value(fu_device_get_context(device), FU_HWIDS_KEY_MANUFACTURER);
	fu_device_build_vendor_id(device, "DMI", manufacturer);
	fu_device_build_vendor_id(device, "OEM", manufacturer);

	fu_device_set_firmware_size_max(device,
					size_max > 0 ? size_max : FU_NVIDIA_OOB_FIRMWARE_SIZE_MAX);
	return TRUE;
}

static FuFirmware *
fu_nvidia_oob_device_prepare_firmware(FuDevice *device,
				      FuInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	/* the PLDM bundle is handed to the BMC unparsed */
	blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, progress, error);
	if (blob == NULL)
		return NULL;
	return fu_firmware_new_from_bytes(blob);
}

gboolean
fu_nvidia_oob_device_refresh_pending(FuNvidiaOobDevice *self, GError **error)
{
	GPtrArray *devices;
	gboolean any_pending = FALSE;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_DEVICE(self), FALSE);

	devices = fu_nvidia_oob_bmc_get_devices(self->bmc);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		const gchar *inventory_uri = fu_device_get_logical_id(device_tmp);
		g_autoptr(FwupdJsonObject) json_member = NULL;
		g_autoptr(GError) error_local = NULL;

		if (inventory_uri == NULL)
			continue;
		if (!fu_nvidia_oob_device_get_json(self,
						   inventory_uri,
						   &json_member,
						   &error_local)) {
			g_debug("failed to re-read %s: %s", inventory_uri, error_local->message);
			continue;
		}
		if (fu_nvidia_oob_device_slot_is_pending(json_member)) {
			g_debug("%s is pending activation", fu_device_get_name(device_tmp));
			fu_device_add_flag(device_tmp, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
			any_pending = TRUE;
		}
	}

	/* the BMC reported nothing staged, so flag the device that was written: one
	 * has to carry the flag for `fwupdmgr activate` to act on, and the aux power
	 * cycle is system-wide anyway */
	if (!any_pending) {
		g_debug("no slot reported PendingActivation, flagging the written device");
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	}
	fu_nvidia_oob_device_set_staged(self);
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);
	const gchar *push_uri = fu_nvidia_oob_bmc_get_push_uri(self->bmc);
	curl_mimepart *part;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *location = NULL;
	g_autoptr(FuNvidiaOobRequest) request = NULL;
	g_autoptr(FwupdJsonArray) json_targets = fwupd_json_array_new();
	g_autoptr(FwupdJsonObject) json_params = fwupd_json_object_new();
	g_autoptr(FwupdRequest) request_activate = fwupd_request_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;
	g_autoptr(curl_mime) mime = NULL;

	/* the upload takes about 30 s and the BMC-side verify and fan-out to the
	 * components about 10 minutes */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "upload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 95, "bmc-apply");

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* the BMC applies every component in the bundle from a single upload, so an
	 * archive naming several components must not send it once per component */
	csum = fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, error);
	if (csum == NULL)
		return FALSE;
	if (g_strcmp0(fu_nvidia_oob_bmc_get_uploaded_checksum(self->bmc), csum) == 0) {
		const gchar *inventory_uri = fu_device_get_logical_id(device);
		g_autoptr(FwupdJsonObject) json_member = NULL;
		g_autoptr(GError) error_slot = NULL;

		if (inventory_uri != NULL &&
		    fu_nvidia_oob_device_get_json(self, inventory_uri, &json_member, &error_slot) &&
		    fu_nvidia_oob_device_slot_is_pending(json_member))
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		g_debug("bundle already uploaded, not resending for %s",
			fu_device_get_name(device));
		fu_progress_finished(progress);
		return TRUE;
	}

	/* Targets is empty as the BMC resolves the components from the bundle,
	 * ForceUpdate bypasses its same-version check, and Immediate is the only
	 * OperationApplyTime it accepts */
	fwupd_json_object_add_array(json_params, "Targets", json_targets);
	fwupd_json_object_add_boolean(json_params, "ForceUpdate", TRUE);
	fwupd_json_object_add_string(json_params, "@Redfish.OperationApplyTime", "Immediate");
	params = fwupd_json_object_to_string(json_params, FWUPD_JSON_EXPORT_FLAG_NONE);

	request = fu_nvidia_oob_bmc_request_new(self->bmc);
	mime = fu_nvidia_oob_request_mime_new(request);
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateParameters");
	(void)curl_mime_type(part, "application/json");
	(void)curl_mime_data(part, params->str, CURL_ZERO_TERMINATED);
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateFile");
	(void)curl_mime_type(part, "application/octet-stream");
	(void)curl_mime_filename(part, "firmware.bin");
	(void)curl_mime_data(part, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));

	g_debug("uploading firmware (%.1f MB) to %s with %s",
		(double)g_bytes_get_size(fw) / (1000.0 * 1000.0),
		push_uri,
		params->str);
	if (!fu_nvidia_oob_request_post_mime(request, push_uri, mime, &location, error))
		return FALSE;
	if (fu_nvidia_oob_request_get_status_code(request) != 202) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to upload firmware: HTTP %li",
			    fu_nvidia_oob_request_get_status_code(request));
		return FALSE;
	}

	/* prefer the Location header, falling back to @odata.id in the response body */
	if (location == NULL || location[0] == '\0') {
		const gchar *odata_id = NULL;
		g_autoptr(FwupdJsonObject) json_resp =
		    fu_nvidia_oob_request_get_json_object(request, NULL);

		/* check the parsed value, as the node may be present but not a string */
		if (json_resp != NULL)
			odata_id = fwupd_json_object_get_string(json_resp, "@odata.id", NULL);
		if (odata_id == NULL || odata_id[0] == '\0') {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no task monitor in the upload response from %s",
				    push_uri);
			return FALSE;
		}
		g_free(location);
		location = g_strdup(odata_id);
	}
	g_debug("task monitor: %s", location);
	fu_progress_step_done(progress);

	if (!fu_nvidia_oob_device_poll_task(self, location, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the upload only stages the firmware, which becomes active when the aux rail
	 * is cycled; this is not done in attach(), which also runs after a failed
	 * write, and not as NEEDS_SHUTDOWN, as a soft power-off activates nothing */
	fu_nvidia_oob_bmc_set_uploaded_checksum(self->bmc, csum);
	if (fu_nvidia_oob_bmc_get_composite(self->bmc)) {
		/* the archive's other components are still to come, and fwupd refuses to
		 * install onto a device already waiting for activation */
		fu_nvidia_oob_bmc_set_written_device(self->bmc, device);
	} else if (!fu_nvidia_oob_device_refresh_pending(self, error)) {
		return FALSE;
	}
	fwupd_request_set_kind(request_activate, FWUPD_REQUEST_KIND_POST);
	fwupd_request_set_id(request_activate, FWUPD_REQUEST_ID_REPLUG_POWER);
	fwupd_request_add_flag(request_activate, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	return fu_device_emit_request(device, request_activate, progress, error);
}

static gchar *
fu_nvidia_oob_device_find_reset_uri(FuNvidiaOobDevice *self, GError **error)
{
	const gchar *target = NULL;
	g_autoptr(FwupdJsonObject) json_actions = NULL;
	g_autoptr(FwupdJsonObject) json_chassis = NULL;
	g_autoptr(FwupdJsonObject) json_oem = NULL;
	g_autoptr(FwupdJsonObject) json_reset = NULL;

	/* read the action target rather than build it, so a BMC that moves it works */
	if (!fu_nvidia_oob_device_get_json(self, FU_NVIDIA_OOB_CHASSIS_URI, &json_chassis, error))
		return NULL;
	json_actions = fwupd_json_object_get_object(json_chassis, "Actions", NULL);
	if (json_actions != NULL)
		json_oem = fwupd_json_object_get_object(json_actions, "Oem", NULL);
	if (json_oem != NULL)
		json_reset =
		    fwupd_json_object_get_object(json_oem, "#NvidiaChassis.AuxPowerReset", NULL);
	if (json_reset != NULL)
		target = fwupd_json_object_get_string(json_reset, "target", NULL);
	if (target == NULL || target[0] == '\0') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s does not advertise NvidiaChassis.AuxPowerReset",
			    FU_NVIDIA_OOB_CHASSIS_URI);
		return NULL;
	}
	return g_strdup(target);
}

static gboolean
fu_nvidia_oob_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);
	glong status_code;
	g_autofree gchar *reset_uri = NULL;
	g_autoptr(FuNvidiaOobRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GError) error_local = NULL;

	reset_uri = fu_nvidia_oob_device_find_reset_uri(self, error);
	if (reset_uri == NULL)
		return FALSE;

	/* AuxPowerCycleForce does not wait for the host to shut down and drops the
	 * rail powering this process, so flush the filesystems while we still can */
	sync();

	fwupd_json_object_add_string(json_obj, "ResetType", "AuxPowerCycleForce");
	request = fu_nvidia_oob_bmc_request_new(self->bmc);
	g_debug("requesting aux power cycle via %s", reset_uri);
	if (!fu_nvidia_oob_request_post(request, reset_uri, json_obj, &error_local)) {
		/* the BMC drops the rail as it acts on the request, so losing the
		 * connection before any status arrives is the expected outcome */
		if (fu_nvidia_oob_request_get_status_code(request) == 0) {
			g_debug("connection lost as the aux rail dropped: %s",
				error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		g_prefix_error_literal(error, "failed to request aux power cycle: ");
		return FALSE;
	}
	status_code = fu_nvidia_oob_request_get_status_code(request);
	if (status_code != 200 && status_code != 202 && status_code != 204) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to request aux power cycle: HTTP %li",
			    status_code);
		return FALSE;
	}
	return TRUE;
}

static void
fu_nvidia_oob_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

FuNvidiaOobDevice *
fu_nvidia_oob_device_new(FuContext *ctx, FuNvidiaOobBmc *bmc, FwupdJsonObject *json_member)
{
	FuNvidiaOobDevice *self = g_object_new(FU_TYPE_NVIDIA_OOB_DEVICE, "context", ctx, NULL);
	self->bmc = g_object_ref(bmc);
	self->json_member = fwupd_json_object_ref(json_member);
	return self;
}

static void
fu_nvidia_oob_device_init(FuNvidiaOobDevice *self)
{
	FuDevice *device = FU_DEVICE(self);

	fu_device_set_summary(device, "OOB-managed firmware (activates on AC cycle)");
	fu_device_add_protocol(device, "org.dmtf.redfish");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);

	/* a full bundle flash takes about 20 minutes */
	fu_device_set_install_duration(device, 2700);

	/* a recovery registered without a function makes fu_device_retry_full() abort
	 * as soon as that error is returned */
	fu_device_retry_add_recovery(device, FWUPD_ERROR, FWUPD_ERROR_WRITE, NULL);
	fu_device_retry_add_recovery(device, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED, NULL);

	/* the install ends with the generic replug-power request: pulling the power
	 * cord cycles the aux rail too, and a custom message would hide releases from
	 * clients that cannot show one, such as fwupdmgr run from a script */
	fu_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);

	/* NEEDS_ACTIVATION is runtime-only, so without this a daemon restart between
	 * staging and activating would strand the staged firmware */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION);

	/* the instance IDs are the same on every OEM's system, so a release must also
	 * match the system's hardware ID */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);

	/* the running version only changes at activation, and the BMC task state
	 * reports a failed write */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
}

static void
fu_nvidia_oob_device_finalize(GObject *object)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(object);
	if (self->json_member != NULL)
		fwupd_json_object_unref(self->json_member);
	if (self->bmc != NULL)
		g_object_unref(self->bmc);
	G_OBJECT_CLASS(fu_nvidia_oob_device_parent_class)->finalize(object);
}

static void
fu_nvidia_oob_device_class_init(FuNvidiaOobDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_nvidia_oob_device_finalize;
	device_class->to_string = fu_nvidia_oob_device_to_string;
	device_class->probe = fu_nvidia_oob_device_probe;
	device_class->prepare_firmware = fu_nvidia_oob_device_prepare_firmware;
	device_class->write_firmware = fu_nvidia_oob_device_write_firmware;
	device_class->activate = fu_nvidia_oob_device_activate;
	device_class->set_progress = fu_nvidia_oob_device_set_progress;
}
