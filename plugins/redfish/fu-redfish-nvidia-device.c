/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>
#include <string.h>
#include <unistd.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-nvidia-device.h"
#include "fu-redfish-nvidia-struct.h"
#include "fu-redfish-request.h"

/* the BMC applies the bundle to each component in turn */
#define FU_REDFISH_NVIDIA_TASK_STATE_POLL_DELAY 2000u /* ms */
#define FU_REDFISH_NVIDIA_TASK_STATE_POLL_CNT	1800u

/* a newly accepted upload needs a few seconds before /Tasks/<id> appears */
#define FU_REDFISH_NVIDIA_TASK_STATE_CREATION_GRACE 5 /* polls */

/* consecutive transient failures tolerated before the update is abandoned */
#define FU_REDFISH_NVIDIA_TASK_STATE_ERRORS_MAX 5

/* cuts and restores the standby power of the whole board, which activates the staged firmware */
#define FU_REDFISH_NVIDIA_AUX_POWER_RESET_URI                                                      \
	"/redfish/v1/Chassis/BMC_0/Actions/Oem/NvidiaChassis.AuxPowerReset"

struct _FuRedfishNvidiaDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishNvidiaDevice, fu_redfish_nvidia_device, FU_TYPE_REDFISH_DEVICE)

typedef struct {
	FuRedfishBackend *backend;
	FuProgress *progress;
	const gchar *task_uri;
	const gchar *monitor_uri;
	guint attempt;
	guint consecutive_errors;
	/* set once /Tasks/<id> has returned a TaskState, to tell a task reaped after
	 * completing apart from one that never existed */
	gboolean saw_persistent_task;
} FuRedfishNvidiaTaskHelper;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

static FwupdJsonObject *
fu_redfish_nvidia_device_get_json_object(FuRedfishNvidiaDevice *self,
					 const gchar *uri,
					 GError **error)
{
	FuRedfishBackend *backend;
	g_autoptr(FuRedfishRequest) request = NULL;

	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return NULL;
	request = fu_redfish_backend_request_new(backend, error);
	if (request == NULL)
		return NULL;
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
					    FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE,
					error))
		return NULL;
	return fu_redfish_request_get_json_object(request, error);
}

static gboolean
fu_redfish_nvidia_device_ensure_parent(FuRedfishNvidiaDevice *self, GError **error)
{
	FuDevice *device = FU_DEVICE(self);
	g_autoptr(FwupdJsonArray) json_arr = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	json_obj = fu_redfish_nvidia_device_get_json_object(self, "/redfish/v1/Managers", error);
	if (json_obj == NULL)
		return FALSE;
	json_arr = fwupd_json_object_get_array(json_obj, "Members", error);
	if (json_arr == NULL)
		return FALSE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_arr); i++) {
		const gchar *bmc_id;
		const gchar *image_uri;
		const gchar *manager_uri;
		g_autoptr(FwupdJsonObject) json_image = NULL;
		g_autoptr(FwupdJsonObject) json_inventory = NULL;
		g_autoptr(FwupdJsonObject) json_links = NULL;
		g_autoptr(FwupdJsonObject) json_manager = NULL;
		g_autoptr(FwupdJsonObject) json_member = NULL;

		json_member = fwupd_json_array_get_object(json_arr, i, error);
		if (json_member == NULL)
			return FALSE;
		manager_uri = fwupd_json_object_get_string(json_member, "@odata.id", error);
		if (manager_uri == NULL)
			return FALSE;
		json_manager = fu_redfish_nvidia_device_get_json_object(self, manager_uri, error);
		if (json_manager == NULL)
			return FALSE;
		if (g_strcmp0(fwupd_json_object_get_string(json_manager, "ManagerType", NULL),
			      "BMC") != 0)
			continue;

		/* the inventory member for the firmware the BMC is running */
		json_links = fwupd_json_object_get_object(json_manager, "Links", NULL);
		if (json_links == NULL)
			continue;
		json_image = fwupd_json_object_get_object(json_links, "ActiveSoftwareImage", NULL);
		if (json_image == NULL)
			continue;
		image_uri = fwupd_json_object_get_string(json_image, "@odata.id", NULL);
		if (image_uri == NULL)
			continue;
		if (g_strcmp0(image_uri, fu_device_get_logical_id(device)) == 0) {
			fu_device_add_private_flag(device,
						   FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN);
			return TRUE;
		}
		json_inventory = fu_redfish_nvidia_device_get_json_object(self, image_uri, error);
		if (json_inventory == NULL)
			return FALSE;
		bmc_id = fwupd_json_object_get_string(json_inventory, "Id", error);
		if (bmc_id == NULL)
			return FALSE;
		fu_device_add_parent_backend_id(device, bmc_id);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_member_is_staged(FwupdJsonObject *json_member)
{
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
	if (g_strcmp0(fwupd_json_object_get_string(json_slot, "FirmwareState", NULL),
		      "PendingActivation") != 0)
		return FALSE;

	/* a slot left empty by a failed update reports the same state */
	version = fwupd_json_object_get_string(json_slot, "Version", NULL);
	return version != NULL && version[0] != '\0';
}

static void
fu_redfish_nvidia_device_ensure_only_explicit(FuRedfishNvidiaDevice *self,
					      FuRedfishBackend *backend)
{
	gboolean any_staged = FALSE;
	g_autoptr(GPtrArray) devices = fu_backend_get_devices(FU_BACKEND(backend));

	/* not yet added when probing */
	if (!g_ptr_array_find(devices, self, NULL))
		g_ptr_array_add(devices, g_object_ref(self));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (FU_IS_REDFISH_NVIDIA_DEVICE(device_tmp) &&
		    fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION))
			any_staged = TRUE;
	}
	if (!any_staged)
		return;

	/* the BMC applies the whole bundle when activated, so stop it being uploaded again
	 * for each other component that it changes */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (!FU_IS_REDFISH_NVIDIA_DEVICE(device_tmp))
			continue;
		if (fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE) ||
		    fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
			fu_device_add_flag(device_tmp, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES);
	}
}

static void
fu_redfish_nvidia_device_ensure_staged(FuRedfishNvidiaDevice *self, FuRedfishBackend *backend)
{
	gboolean any_staged = FALSE;
	g_autoptr(GPtrArray) devices = fu_backend_get_devices(FU_BACKEND(backend));

	/* the inventory has changed */
	fu_backend_invalidate(FU_BACKEND(backend));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		g_autoptr(FwupdJsonObject) json_member = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!FU_IS_REDFISH_NVIDIA_DEVICE(device_tmp))
			continue;
		json_member =
		    fu_redfish_nvidia_device_get_json_object(FU_REDFISH_NVIDIA_DEVICE(device_tmp),
							     fu_device_get_logical_id(device_tmp),
							     &error_local);
		if (json_member == NULL) {
			g_debug("ignoring %s: %s",
				fu_device_get_logical_id(device_tmp),
				error_local->message);
			continue;
		}
		if (fu_redfish_nvidia_device_member_is_staged(json_member)) {
			fu_device_add_flag(device_tmp, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
			any_staged = TRUE;
		} else {
			fu_device_remove_flag(device_tmp, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		}
	}

	/* something has to carry the flag for the activation */
	if (!any_staged)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_redfish_nvidia_device_ensure_only_explicit(self, backend);
}

static gboolean
fu_redfish_nvidia_device_probe(FuDevice *device, GError **error)
{
	FuRedfishNvidiaDevice *self = FU_REDFISH_NVIDIA_DEVICE(device);
	FuRedfishBackend *backend;
	FwupdJsonObject *json_member = NULL;

	/* FuRedfishDevice->probe */
	if (!FU_DEVICE_CLASS(fu_redfish_nvidia_device_parent_class)->probe(device, error))
		return FALSE;

	/* every member reports the same Name */
	if (fu_device_get_backend_id(device) != NULL)
		fu_device_set_name(device, fu_device_get_backend_id(device));

	/* override fu_redfish_device_set_version() */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);

	/* firmware staged by an earlier install */
	g_object_get(device, "member", &json_member, NULL);
	if (fu_redfish_nvidia_device_member_is_staged(json_member))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;
	fu_redfish_nvidia_device_ensure_only_explicit(self, backend);

	/* every other component is a child of the BMC */
	return fu_redfish_nvidia_device_ensure_parent(self, error);
}

static size_t
fu_redfish_nvidia_device_location_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	char **location = (char **)userdata;
	if ((size * nmemb) > 16 && g_ascii_strncasecmp(ptr, "Location:", 9) == 0)
		*location = g_strndup(ptr + 10, (size * nmemb) - 12);
	return size * nmemb;
}

static gchar *
fu_redfish_nvidia_device_derive_task_uri(const gchar *monitor_uri)
{
	const gchar *suffix = "/Monitor";
	if (!g_str_has_suffix(monitor_uri, suffix))
		return NULL;
	return g_strndup(monitor_uri, strlen(monitor_uri) - strlen(suffix));
}

static gboolean
fu_redfish_nvidia_device_task_state_is_running(const gchar *state)
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
fu_redfish_nvidia_device_parse_task(const gchar *task_uri,
				    FwupdJsonObject *json_task,
				    FuRedfishNvidiaTaskState *task_state,
				    guint *percentage,
				    GError **error)
{
	const gchar *state;
	const gchar *status;
	gint64 percent = 0;

	if (!fwupd_json_object_get_integer_with_default(json_task,
							"PercentComplete",
							&percent,
							0,
							error))
		return FALSE;
	if (percentage != NULL)
		*percentage = (guint)CLAMP(percent, 0, 100);

	state = fwupd_json_object_get_string(json_task, "TaskState", error);
	if (state == NULL) {
		g_prefix_error(error, "BMC task %s has no TaskState: ", task_uri);
		return FALSE;
	}

	/* the BMC sets TaskStatus=Critical once the operation has failed, even while
	 * TaskState still shows Running */
	status = fwupd_json_object_get_string(json_task, "TaskStatus", error);
	if (status == NULL)
		return FALSE;
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
			*task_state = FU_REDFISH_NVIDIA_TASK_STATE_COMPLETED;
		return TRUE;
	}

	if (!fu_redfish_nvidia_device_task_state_is_running(state)) {
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
		*task_state = percent == 100 ? FU_REDFISH_NVIDIA_TASK_STATE_COMPLETED
					     : FU_REDFISH_NVIDIA_TASK_STATE_RUNNING;
	}
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_request_check(FuRedfishRequest *request, GError **error)
{
	glong status_code = fu_redfish_request_get_status_code(request);

	/* fatal */
	if (status_code == 401 || status_code == 403) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "authentication failed with HTTP %li",
			    status_code);
		return FALSE;
	}

	/* transient */
	if (status_code == 0 || status_code == 408 || status_code == 425 || status_code == 429 ||
	    status_code >= 500) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "transient HTTP %li",
			    status_code);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_poll_request(FuRedfishBackend *backend,
				      const gchar *uri,
				      gboolean allow_empty_200,
				      FuRedfishNvidiaTaskResponse *response,
				      FwupdJsonObject **json_task,
				      GError **error)
{
	glong status_code;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_tmp = NULL;
	g_autoptr(GError) error_local = NULL;

	request = fu_redfish_backend_request_new(backend, error);
	if (request == NULL)
		return FALSE;
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					&error_local)) {
		/* these clear while an update is active */
		if (!fu_redfish_nvidia_device_request_check(request, error)) {
			g_prefix_error(error, "polling task %s: ", uri);
			return FALSE;
		}
		/* a 2xx response that failed transport or JSON validation is not a task */
		if (fu_redfish_request_get_status_code(request) == 404) {
			*response = FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	status_code = fu_redfish_request_get_status_code(request);
	json_tmp = fu_redfish_request_get_json_object(request, NULL);
	if (json_tmp == NULL) {
		/* once the task is done the task monitor answers 200 with an empty body */
		if (status_code == 200 && allow_empty_200) {
			*response = FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;
			return TRUE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "transient HTTP %li polling task %s",
			    status_code,
			    uri);
		return FALSE;
	}

	/* a reap status wins over any body the BMC happens to return */
	if (status_code == 404) {
		*response = FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;
		return TRUE;
	}

	/* these statuses can clear while an update remains active */
	if (!fu_redfish_nvidia_device_request_check(request, error)) {
		g_prefix_error(error, "polling task %s: ", uri);
		return FALSE;
	}

	/* never let task-shaped JSON turn an HTTP error into success */
	if (status_code < 200 || status_code >= 300) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "unexpected HTTP %li polling task %s",
			    status_code,
			    uri);
		return FALSE;
	}

	/* success */
	*response = FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK;
	*json_task = g_steal_pointer(&json_tmp);
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_poll_error(FuRedfishNvidiaTaskHelper *helper,
				    GError *error_local,
				    GError **error)
{
	g_autoptr(GError) error_taken = error_local;

	if (!g_error_matches(error_taken, FWUPD_ERROR, FWUPD_ERROR_BUSY)) {
		g_propagate_error(error, g_steal_pointer(&error_taken));
		return FALSE;
	}
	if (++helper->consecutive_errors > FU_REDFISH_NVIDIA_TASK_STATE_ERRORS_MAX) {
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
fu_redfish_nvidia_device_poll_task_reaped(FuRedfishNvidiaTaskHelper *helper, GError **error)
{
	FuRedfishNvidiaTaskResponse response;
	FuRedfishNvidiaTaskState task_state;
	guint percentage = 0;
	g_autoptr(FwupdJsonObject) json_task = NULL;
	g_autoptr(GError) error_local = NULL;

	/* the persistent task is gone, so check the monitor before declaring success,
	 * as a live monitor can still report a failure */
	if (helper->monitor_uri != NULL && !g_str_equal(helper->task_uri, helper->monitor_uri)) {
		if (!fu_redfish_nvidia_device_poll_request(helper->backend,
							   helper->monitor_uri,
							   TRUE,
							   &response,
							   &json_task,
							   &error_local)) {
			return fu_redfish_nvidia_device_poll_error(helper,
								   g_steal_pointer(&error_local),
								   error);
		}
		if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK) {
			if (!fu_redfish_nvidia_device_parse_task(helper->monitor_uri,
								 json_task,
								 &task_state,
								 &percentage,
								 error))
				return FALSE;
			fu_progress_set_percentage(helper->progress, percentage);
			if (task_state == FU_REDFISH_NVIDIA_TASK_STATE_COMPLETED) {
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

	/* both are gone after at least one valid poll of the persistent task, so the
	 * BMC has finished with them */
	if (helper->saw_persistent_task) {
		g_debug("task and monitor reaped after a valid poll, treating as completed");
		fu_progress_set_percentage(helper->progress, 100);
		return TRUE;
	}

	if (helper->attempt <= FU_REDFISH_NVIDIA_TASK_STATE_CREATION_GRACE) {
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
fu_redfish_nvidia_device_poll_task_once(FuRedfishNvidiaTaskHelper *helper, GError **error)
{
	FuRedfishNvidiaTaskResponse response;
	FuRedfishNvidiaTaskState task_state;
	gboolean is_monitor = g_str_has_suffix(helper->task_uri, "/Monitor");
	guint percentage = 0;
	g_autoptr(FwupdJsonObject) json_task = NULL;
	g_autoptr(GError) error_local = NULL;

	if (!fu_redfish_nvidia_device_poll_request(helper->backend,
						   helper->task_uri,
						   is_monitor,
						   &response,
						   &json_task,
						   &error_local)) {
		return fu_redfish_nvidia_device_poll_error(helper,
							   g_steal_pointer(&error_local),
							   error);
	}
	if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED)
		return fu_redfish_nvidia_device_poll_task_reaped(helper, error);

	if (!fu_redfish_nvidia_device_parse_task(helper->task_uri,
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
			helper->attempt * FU_REDFISH_NVIDIA_TASK_STATE_POLL_DELAY / 1000);
	}

	if (task_state == FU_REDFISH_NVIDIA_TASK_STATE_COMPLETED) {
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
fu_redfish_nvidia_device_poll_task_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRedfishNvidiaTaskHelper *helper = (FuRedfishNvidiaTaskHelper *)user_data;
	g_autoptr(GError) error_local = NULL;

	helper->attempt++;
	if (fu_redfish_nvidia_device_poll_task_once(helper, &error_local))
		return TRUE;

	/* fu_device_retry_full() retries any error without a recovery registered in
	 * _init(), so turn everything the poll does not want retried into
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
fu_redfish_nvidia_device_poll_task(FuRedfishNvidiaDevice *self,
				   FuRedfishBackend *backend,
				   const gchar *location,
				   FuProgress *progress,
				   GError **error)
{
	FuRedfishNvidiaTaskHelper helper = {
	    .backend = backend,
	    .progress = progress,
	    .monitor_uri = location,
	};
	/* PercentComplete is only updated on the persistent /Tasks/<id> resource, so
	 * poll that, keeping the monitor to detect its reap separately */
	g_autofree gchar *persistent_uri = fu_redfish_nvidia_device_derive_task_uri(location);

	helper.task_uri = persistent_uri != NULL ? persistent_uri : location;
	g_debug("polling task %s for up to %u s",
		helper.task_uri,
		FU_REDFISH_NVIDIA_TASK_STATE_POLL_CNT * FU_REDFISH_NVIDIA_TASK_STATE_POLL_DELAY /
		    1000);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_redfish_nvidia_device_poll_task_cb,
				  FU_REDFISH_NVIDIA_TASK_STATE_POLL_CNT,
				  FU_REDFISH_NVIDIA_TASK_STATE_POLL_DELAY,
				  &helper,
				  error)) {
		g_prefix_error(error, "failed to poll BMC task %s: ", helper.task_uri);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuRedfishBackend *backend;
	CURL *curl;
	curl_mimepart *part;
	g_autofree gchar *location = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonArray) json_targets = fwupd_json_array_new();
	g_autoptr(FwupdJsonObject) json_params = fwupd_json_object_new();
	g_autoptr(FwupdRequest) request_power = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;
	g_autoptr(curl_mime) mime = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "upload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "bmc-apply");

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;

	/* Targets is empty as the BMC takes the components from the bundle manifest,
	 * ForceUpdate bypasses its same-version check, and Immediate is the only
	 * apply time it accepts */
	fwupd_json_object_add_array(json_params, "Targets", json_targets);
	fwupd_json_object_add_boolean(json_params, "ForceUpdate", TRUE);
	fwupd_json_object_add_string(json_params, "@Redfish.OperationApplyTime", "Immediate");
	params = fwupd_json_object_to_string(json_params, FWUPD_JSON_EXPORT_FLAG_INDENT);

	/* build the multipart POST */
	request = fu_redfish_backend_request_new(backend, error);
	if (request == NULL)
		return FALSE;
	curl = fu_redfish_request_get_curl(request);
	mime = curl_mime_init(curl);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateParameters");
	(void)curl_mime_type(part, "application/json");
	(void)curl_mime_data(part, params->str, CURL_ZERO_TERMINATED);
	g_debug("UpdateParameters: %s", params->str);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateFile");
	(void)curl_mime_type(part, "application/octet-stream");
	(void)curl_mime_filename(part, "firmware.bin");
	(void)curl_mime_data(part, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));

	(void)curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
	(void)curl_easy_setopt(curl,
			       CURLOPT_HEADERFUNCTION,
			       fu_redfish_nvidia_device_location_header_cb);
	(void)curl_easy_setopt(curl, CURLOPT_HEADERDATA, &location);
	(void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, (glong)600);

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;
	if (fu_redfish_request_get_status_code(request) != 202) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to upload firmware: HTTP %li",
			    fu_redfish_request_get_status_code(request));
		return FALSE;
	}

	/* prefer the Location header, falling back to @odata.id in the response body */
	if (location == NULL || location[0] == '\0') {
		const gchar *odata_id = NULL;
		g_autoptr(FwupdJsonObject) json_resp =
		    fu_redfish_request_get_json_object(request, NULL);

		if (json_resp != NULL)
			odata_id = fwupd_json_object_get_string(json_resp, "@odata.id", NULL);
		if (odata_id == NULL || odata_id[0] == '\0') {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no TaskMonitor location in upload response from %s",
				    fu_redfish_backend_get_push_uri_path(backend));
			return FALSE;
		}
		g_free(location);
		location = g_strdup(odata_id);
	}
	g_debug("TaskMonitor location: %s", location);
	fu_progress_step_done(progress);

	/* the bundle has been uploaded, so free it before the long wait */
	g_clear_object(&request);
	g_clear_pointer(&mime, curl_mime_free);
	g_clear_pointer(&fw, g_bytes_unref);

	/* wait for the BMC to apply the bundle */
	if (!fu_redfish_nvidia_device_poll_task(FU_REDFISH_NVIDIA_DEVICE(device),
						backend,
						location,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the BMC chooses the components from the bundle, so ask it which ones it staged */
	fu_redfish_nvidia_device_ensure_staged(FU_REDFISH_NVIDIA_DEVICE(device), backend);

	/* pulling the power cable activates the firmware too */
	request_power = fwupd_request_new();
	fwupd_request_set_kind(request_power, FWUPD_REQUEST_KIND_POST);
	fwupd_request_set_id(request_power, FWUPD_REQUEST_ID_REPLUG_POWER);
	fwupd_request_add_flag(request_power, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	return fu_device_emit_request(device, request_power, progress, error);
}

static gboolean
fu_redfish_nvidia_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRedfishBackend *backend;
	glong status_code;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend, error);
	if (request == NULL)
		return FALSE;

	/* the host may not have finished shutting down when the BMC cuts the power */
	sync();

	fwupd_json_object_add_string(json_obj, "ResetType", "AuxPowerCycleForce");
	if (!fu_redfish_request_perform_full(request,
					     FU_REDFISH_NVIDIA_AUX_POWER_RESET_URI,
					     "POST",
					     json_obj,
					     FU_REDFISH_REQUEST_PERFORM_FLAG_NONE,
					     error)) {
		g_prefix_error_literal(error, "failed to request an AC power cycle: ");
		return FALSE;
	}
	status_code = fu_redfish_request_get_status_code(request);
	if (status_code < 200 || status_code >= 300) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to request an AC power cycle: HTTP %li",
			    status_code);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_redfish_nvidia_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_redfish_nvidia_device_init(FuRedfishNvidiaDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish NVIDIA device");
	fu_device_add_private_flag(FU_DEVICE(self), FU_REDFISH_DEVICE_FLAG_RELATED_CHASSIS_PCIE);

	/* one upload of the bundle installs every component */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_COMPOSITE_SINGLE_PAYLOAD);

	/* the BMC stages the firmware, so no version changes until it is activated */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);

	/* the install ends by asking for the power cable to be pulled */
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);

	/* the task poll turns every error it must not retry into one of these */
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_WRITE, NULL);
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED, NULL);
}

static void
fu_redfish_nvidia_device_class_init(FuRedfishNvidiaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_redfish_nvidia_device_probe;
	device_class->write_firmware = fu_redfish_nvidia_device_write_firmware;
	device_class->activate = fu_redfish_nvidia_device_activate;
	device_class->set_progress = fu_redfish_nvidia_device_set_progress;
}
