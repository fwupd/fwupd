/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * FuRedfishNvidiaDevice — NVIDIA DGX Station GB300 OOB firmware update via Redfish.
 *
 * Represents a single component in the BMC's FirmwareInventory (e.g.
 * /redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0) and implements the
 * GB300-specific upload and task-monitoring flow:
 *
 *   1. Multipart POST to UpdateService/MultipartHttpPushUri:
 *        UpdateParameters: { "Targets":[], "ForceUpdate":true,
 *                            "@Redfish.OperationApplyTime":"Immediate" }
 *        UpdateFile: raw firmware blob
 *
 *   2. Poll /redfish/v1/TaskService/Tasks/<id> (the persistent task resource,
 *      not the /Monitor sub-resource) for PercentComplete and TaskState.
 *      GB300-specific divergences from DMTF DSP0266:
 *        - Targets must be an empty array, as the BMC resolves components from
 *          the PLDM bundle manifest; passing the logical_id causes a 400 error.
 *        - ForceUpdate=true is required; the BMC rejects same-version installs.
 *        - OperationApplyTime "OnReset" is not in the BMC's acceptable-values
 *          list; "Immediate" must be used.
 *        - PercentComplete is only updated on /Tasks/<id>, not on
 *          /Tasks/<id>/Monitor; polling the Monitor always returns 0%.
 *        - After task completion the BMC returns HTTP 200 and an empty body for
 *          the TaskMonitor instead of the canonical 404 that signals reap; both
 *          are handled here. When both the Monitor and /Tasks/<id> are gone the
 *          resource-gone condition is treated as Completed.
 *
 *   3. A completed write sets NEEDS_ACTIVATION, as the firmware is staged and
 *      only becomes active after an aux-rail power cycle. activate() drives
 *      that cycle by POSTing ResetType=AuxPowerCycleForce to the OEM
 *      NvidiaChassis.AuxPowerReset action advertised by Chassis/BMC_0. The
 *      Force variant does not wait for the host to shut down, so it drops the
 *      rail powering fwupd itself.
 *
 * Instance IDs registered per device, in addition to those from the base class:
 *   NVIDIA_OOB\URI_<@odata.id>   — always; unique per FirmwareInventory entry
 *   NVIDIA_OOB\SWID_<SoftwareId> — when present; lets a unified-image CAB target
 *                                  multiple components by SoftwareId
 *
 * Backend detection: fu_redfish_backend_coldplug() sets device_gtype to
 * FU_TYPE_REDFISH_NVIDIA_DEVICE when /redfish/v1/Chassis/Chassis_0 reports
 * Manufacturer="NVIDIA" and a Model containing both "GB300" and "Station".
 *
 * This device requires a Redfish X-Auth-Token provisioned out of band; see
 * README.md for how to set FWUPD_SESSION_TOKEN_FILE. Without a token the
 * plugin falls back to Basic Auth.
 */

#include "config.h"

#include <curl/curl.h>
#include <string.h>
#include <unistd.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-device.h"
#include "fu-redfish-nvidia-device.h"
#include "fu-redfish-request.h"

/* 1800 polls at 2s covers the worst-case GB300 bundle flash of ~60 minutes */
#define FU_REDFISH_NVIDIA_TASK_POLL_DELAY 2000u /* ms */
#define FU_REDFISH_NVIDIA_TASK_POLL_CNT	  1800u

/* a newly accepted upload needs a few seconds before /Tasks/<id> appears */
#define FU_REDFISH_NVIDIA_TASK_CREATION_GRACE 5 /* polls */

/* consecutive transient failures tolerated before the update is abandoned */
#define FU_REDFISH_NVIDIA_TASK_ERRORS_MAX 5

/* only used when the BMC UpdateService does not report MaxImageSizeBytes */
#define FU_REDFISH_NVIDIA_FIRMWARE_SIZE_MAX (256 * FU_MB)

/* the chassis carrying the OEM aux-rail reset, which is not the one used to
 * detect the GB300 */
#define FU_REDFISH_NVIDIA_CHASSIS_URI "/redfish/v1/Chassis/BMC_0"

struct _FuRedfishNvidiaDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishNvidiaDevice, fu_redfish_nvidia_device, FU_TYPE_REDFISH_DEVICE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

typedef struct {
	FuRedfishBackend *backend;
	FuProgress *progress;
	const gchar *task_uri;
	const gchar *monitor_uri;
	guint attempt;
	guint consecutive_errors;
	/* set once the persistent /Tasks/<id> URI has returned a valid TaskState, to
	 * tell "task reaped after completing" apart from "task never existed"; the
	 * monitor URI returning a live response does not set this */
	gboolean saw_persistent_task;
} FuRedfishNvidiaTaskHelper;

/* same pattern as FuRedfishMultipartDevice */
static size_t
fu_redfish_nvidia_device_location_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	char **location = (char **)userdata;
	if ((size * nmemb) > 16 && g_ascii_strncasecmp(ptr, "Location:", 9) == 0)
		*location = g_strndup(ptr + 10, (size * nmemb) - 12);
	return size * nmemb;
}

/* derive the persistent /Tasks/<id> URI from a /Tasks/<id>/Monitor URI, returning
 * NULL when @monitor_uri does not end in /Monitor */
static gchar *
fu_redfish_nvidia_device_derive_task_uri(const gchar *monitor_uri)
{
	const gchar *suffix = "/Monitor";
	if (!g_str_has_suffix(monitor_uri, suffix))
		return NULL;
	return g_strndup(monitor_uri, strlen(monitor_uri) - strlen(suffix));
}

FuRedfishNvidiaTaskResponse
fu_redfish_nvidia_device_classify_task_response(glong status_code,
						gboolean request_succeeded,
						gboolean has_json,
						gboolean allow_empty_200)
{
	/* a reap status wins over any body the BMC happens to return */
	if (status_code == 404)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;

	/* these statuses can clear while an update remains active */
	if (status_code == 0 || status_code == 408 || status_code == 425 || status_code == 429 ||
	    status_code >= 500)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT;

	/* never let task-shaped JSON turn an HTTP error into success */
	if (status_code < 200 || status_code >= 300)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL;

	/* a 2xx response that failed transport or JSON validation is not a task */
	if (!request_succeeded)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL;

	if (!has_json) {
		/* a documented GB300 quirk of the TaskMonitor only */
		if (status_code == 200 && allow_empty_200)
			return FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT;
	}

	return FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK;
}

static gboolean
fu_redfish_nvidia_device_task_state_is_running(const gchar *state)
{
	return g_strcmp0(state, "New") == 0 || g_strcmp0(state, "Starting") == 0 ||
	       g_strcmp0(state, "Running") == 0 || g_strcmp0(state, "Suspended") == 0 ||
	       g_strcmp0(state, "Pending") == 0 || g_strcmp0(state, "Stopping") == 0 ||
	       g_strcmp0(state, "Service") == 0 || g_strcmp0(state, "Cancelling") == 0 ||
	       g_strcmp0(state, "Continue") == 0;
}

/* parse TaskState and PercentComplete from a task JSON object, returning %FALSE
 * with @error set when the task has failed or cannot be understood */
gboolean
fu_redfish_nvidia_device_parse_task(const gchar *task_uri,
				    FwupdJsonObject *json_task,
				    FuRedfishNvidiaTaskState *task_state,
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
	 * failed, even while TaskState still shows Running, so reject it here rather
	 * than wait for a state transition that will never arrive */
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
			*task_state = FU_REDFISH_NVIDIA_TASK_COMPLETED;
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

	/* some GB300 BMC versions report 100% before leaving a known running state */
	if (task_state != NULL) {
		*task_state = percent == 100 ? FU_REDFISH_NVIDIA_TASK_COMPLETED
					     : FU_REDFISH_NVIDIA_TASK_RUNNING;
	}
	return TRUE;
}

/*
 * Poll @uri once and classify the result.
 *
 * Returns %TRUE when @response is TASK or REAPED, and %FALSE with @error set
 * otherwise. FWUPD_ERROR_BUSY marks the conditions the caller may retry; every
 * other error code aborts the poll.
 */
static gboolean
fu_redfish_nvidia_device_poll_request(FuRedfishBackend *backend,
				      const gchar *uri,
				      gboolean allow_empty_200,
				      FuRedfishNvidiaTaskResponse *response,
				      FwupdJsonObject **json_task,
				      GError **error)
{
	gboolean request_succeeded;
	glong status_code;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);
	g_autoptr(FwupdJsonObject) json_tmp = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(response != NULL, FALSE);
	g_return_val_if_fail(json_task != NULL, FALSE);
	g_return_val_if_fail(*json_task == NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	request_succeeded = fu_redfish_request_perform(request,
						       uri,
						       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
						       &error_local);
	status_code = fu_redfish_request_get_status_code(request);
	json_tmp = fu_redfish_request_get_json_object(request);
	*response = fu_redfish_nvidia_device_classify_task_response(status_code,
								    request_succeeded,
								    json_tmp != NULL,
								    allow_empty_200);

	if (*response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK) {
		*json_task = g_steal_pointer(&json_tmp);
		return TRUE;
	}
	if (*response == FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED)
		return TRUE;
	if (*response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT) {
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

/*
 * Account for a failed poll, always returning %FALSE with @error set.
 *
 * Transient failures are tolerated so that brief connectivity blips during a
 * flash lasting tens of minutes do not abandon a still-running update. The
 * counter is reset by every poll that returns a valid task body, so only
 * consecutive failures abort.
 */
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
	if (++helper->consecutive_errors > FU_REDFISH_NVIDIA_TASK_ERRORS_MAX) {
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

/* the persistent task is gone, so validate the monitor before declaring success
 * as a live monitor can still report a terminal failure */
static gboolean
fu_redfish_nvidia_device_poll_task_reaped(FuRedfishNvidiaTaskHelper *helper, GError **error)
{
	FuRedfishNvidiaTaskResponse response;
	FuRedfishNvidiaTaskState task_state;
	guint percentage = 0;
	g_autoptr(FwupdJsonObject) json_task = NULL;
	g_autoptr(GError) error_local = NULL;

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
			if (task_state == FU_REDFISH_NVIDIA_TASK_COMPLETED) {
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
	 * task, so the BMC has completed the PLDM activation lifecycle and reaped them */
	if (helper->saw_persistent_task) {
		g_debug("task and monitor reaped after a valid poll, treating as completed");
		fu_progress_set_percentage(helper->progress, 100);
		return TRUE;
	}

	if (helper->attempt <= FU_REDFISH_NVIDIA_TASK_CREATION_GRACE) {
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
			helper->attempt * FU_REDFISH_NVIDIA_TASK_POLL_DELAY / 1000);
	}

	if (task_state == FU_REDFISH_NVIDIA_TASK_COMPLETED) {
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

	/* fu_device_retry_full() retries any error that has no recovery registered in
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
	/* the GB300 BMC only updates PercentComplete on the persistent /Tasks/<id>
	 * resource, so prefer it for polling but keep the monitor URI to detect its
	 * reap separately and to fall back to while /Tasks/<id> is being created */
	g_autofree gchar *persistent_uri = fu_redfish_nvidia_device_derive_task_uri(location);

	helper.task_uri = persistent_uri != NULL ? persistent_uri : location;
	g_debug("polling task %s for up to %u s",
		helper.task_uri,
		FU_REDFISH_NVIDIA_TASK_POLL_CNT * FU_REDFISH_NVIDIA_TASK_POLL_DELAY / 1000);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_redfish_nvidia_device_poll_task_cb,
				  FU_REDFISH_NVIDIA_TASK_POLL_CNT,
				  FU_REDFISH_NVIDIA_TASK_POLL_DELAY,
				  &helper,
				  error)) {
		g_prefix_error(error, "failed to poll BMC task %s: ", helper.task_uri);
		return FALSE;
	}
	return TRUE;
}

/*
 * Has the BMC staged an image for this component that is waiting on the
 * aux-rail power cycle?
 *
 * The OEM slot data is the authoritative answer, and asking the BMC each time
 * the device is probed means a pending activation survives a daemon restart,
 * a host reboot, and a same-version reinstall -- none of which the engine's
 * own history can express.
 */
static gboolean
fu_redfish_nvidia_device_slot_is_pending(FwupdJsonObject *json_member)
{
	const gchar *state;
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
	return g_strcmp0(state, "PendingActivation") == 0;
}

static gboolean
fu_redfish_nvidia_device_probe(FuDevice *device, GError **error)
{
	FuRedfishDevice *rdev = FU_REDFISH_DEVICE(device);
	FwupdJsonObject *member;
	const gchar *inventory_uri;
	const gchar *software_id;
	g_autofree gchar *basename = NULL;

	/* the base probe sets logical_id, vendor, version, name and the
	 * REDFISH\VENDOR\SOFTWAREID instance IDs */
	if (!FU_DEVICE_CLASS(fu_redfish_nvidia_device_parent_class)->probe(device, error))
		return FALSE;

	/* after the base probe, logical_id holds the @odata.id inventory URI */
	inventory_uri = fu_device_get_logical_id(device);
	if (inventory_uri == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "FirmwareInventory member has no @odata.id");
		return FALSE;
	}

	/* the BMC returns Name="Software Inventory" for every entry, which makes all
	 * 12+ OOB components indistinguishable; the inventory URI basename is unique
	 * and operator-meaningful, and fu_device_set_name() renders the underscores
	 * as spaces */
	basename = g_path_get_basename(inventory_uri);
	if (basename != NULL && basename[0] != '\0' && g_strcmp0(basename, ".") != 0 &&
	    g_strcmp0(basename, "/") != 0)
		fu_device_set_name(device, basename);

	/* globally unique per FirmwareInventory entry, and the primary match key for
	 * a per-device LVFS CAB targeting one component */
	fu_device_add_instance_str(device, "URI", inventory_uri);
	if (!fu_device_build_instance_id(device, error, "NVIDIA_OOB", "URI", NULL))
		return FALSE;

	/* lets a unified-image CAB target multiple components sharing a SoftwareId,
	 * e.g. FW_ERoT_CPU_0 and FW_ERoT_FPGA_0 both carry SoftwareId=0xFF00 */
	member = fu_redfish_device_get_json_obj_member(rdev);
	if (member != NULL) {
		software_id = fwupd_json_object_get_string(member, "SoftwareId", NULL);
		if (software_id != NULL && software_id[0] != '\0') {
			fu_device_add_instance_str(device, "SWID", software_id);
			if (!fu_device_build_instance_id(device, error, "NVIDIA_OOB", "SWID", NULL))
				return FALSE;
		}
		if (fu_redfish_nvidia_device_slot_is_pending(member))
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	}

	/* DMI:NVIDIA pairs with the GB300 SMBIOS identity, and OEM:NVIDIA is the
	 * non-bus fallback as OOB-managed components have no host bus identity */
	fu_device_add_vendor_id(device, "DMI:NVIDIA");
	fu_device_add_vendor_id(device, "OEM:NVIDIA");

	/* the backend replaces this with MaxImageSizeBytes when the BMC reports one */
	fu_device_set_firmware_size_max(device, FU_REDFISH_NVIDIA_FIRMWARE_SIZE_MAX);

	/* a full PLDM bundle flash across all components takes ~20 minutes in
	 * practice, and 45 minutes leaves headroom for slower runs */
	fu_device_set_install_duration(device, 2700);
	fu_device_set_summary(device, "OOB-managed firmware (activates on AC cycle)");

	return TRUE;
}

/*
 * The PLDM bundle is an opaque blob of about 117MB, which is larger than the
 * FuFirmware base-class parse ceiling of FU_FIRMWARE_SIZE_MAX_DEFAULT, so read
 * it straight into a FuFirmware rather than parsing it. The real limit is the
 * device firmware size max set in probe(), which fu_device_prepare_firmware()
 * applies to the result.
 */
static FuFirmware *
fu_redfish_nvidia_device_prepare_firmware(FuDevice *device,
					  FuInputStream *stream,
					  FuProgress *progress,
					  FuFirmwareParseFlags flags,
					  GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, progress, error);
	if (blob == NULL)
		return NULL;
	return fu_firmware_new_from_bytes(blob);
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
	g_autoptr(FwupdRequest) request_activate = fwupd_request_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;
	g_autoptr(curl_mime) mime = NULL;

	/* the upload is fast (~30s for a ~117MB bundle) and the BMC-side PLDM verify
	 * and per-component fan-out takes ~10 minutes, so weighting the two steps
	 * 5/95 keeps the rate-based ETA from fwupdmgr close to install_duration */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "upload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 95, "bmc-apply");

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* Targets is empty as the BMC resolves components from the PLDM bundle
	 * manifest, ForceUpdate bypasses the same-version check, and Immediate is
	 * the only OperationApplyTime the BMC accepts */
	fwupd_json_object_add_array(json_params, "Targets", json_targets);
	fwupd_json_object_add_boolean(json_params, "ForceUpdate", TRUE);
	fwupd_json_object_add_string(json_params, "@Redfish.OperationApplyTime", "Immediate");
	params = fwupd_json_object_to_string(json_params, FWUPD_JSON_EXPORT_FLAG_INDENT);

	/* build the multipart POST */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
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
	/* ~117MB over USB-OOB at ~1-2MB/s */
	(void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, (glong)600);

	g_debug("uploading firmware (%.1f MB) to %s",
		(double)g_bytes_get_size(fw) / (1000.0 * 1000.0),
		fu_redfish_backend_get_push_uri_path(backend));
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
		g_autoptr(FwupdJsonObject) json_resp = fu_redfish_request_get_json_object(request);
		const gchar *odata_id = NULL;

		/* check the parsed value rather than has_node(), as the node may be
		 * present but not a string, which would leave location NULL */
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

	if (!fu_redfish_nvidia_device_poll_task(FU_REDFISH_NVIDIA_DEVICE(device),
						backend,
						location,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the upload only stages the firmware; it becomes active when the BMC cycles
	 * the aux rail, which activate() asks it to do.
	 *
	 * This is deliberately here rather than in attach(), which the engine also
	 * runs after a failed write to return the device to runtime -- marking a
	 * failed update as pending activation would ask the user to power-cycle the
	 * machine for firmware that was never staged.
	 *
	 * Deliberately not NEEDS_SHUTDOWN either: that makes the client offer a soft
	 * poweroff, which leaves the aux rail energised and so activates nothing,
	 * while steering the user away from the activation that does work. */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fwupd_request_set_kind(request_activate, FWUPD_REQUEST_KIND_POST);
	fwupd_request_set_id(request_activate, FWUPD_REQUEST_ID_REPLUG_POWER);
	fwupd_request_add_flag(request_activate, FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE);
	fwupd_request_set_message(request_activate,
				  "The staged firmware becomes active after an aux-rail power "
				  "cycle. Run `fwupdmgr activate` to ask the BMC to cycle the "
				  "rail, which powers the system off immediately -- save your "
				  "work first.");
	return fu_device_emit_request(device, request_activate, progress, error);
}

/* return the target of the OEM AuxPowerReset action advertised by @json_chassis */
static gchar *
fu_redfish_nvidia_device_chassis_reset_uri(FwupdJsonObject *json_chassis)
{
	const gchar *target;
	g_autoptr(FwupdJsonObject) json_actions = NULL;
	g_autoptr(FwupdJsonObject) json_oem = NULL;
	g_autoptr(FwupdJsonObject) json_reset = NULL;

	json_actions = fwupd_json_object_get_object(json_chassis, "Actions", NULL);
	if (json_actions == NULL)
		return NULL;
	json_oem = fwupd_json_object_get_object(json_actions, "Oem", NULL);
	if (json_oem == NULL)
		return NULL;
	json_reset = fwupd_json_object_get_object(json_oem, "#NvidiaChassis.AuxPowerReset", NULL);
	if (json_reset == NULL)
		return NULL;
	target = fwupd_json_object_get_string(json_reset, "target", NULL);
	if (target == NULL || target[0] == '\0')
		return NULL;
	return g_strdup(target);
}

/* read the action target from the BMC chassis rather than building the URI here,
 * so that a BMC which moves the action still works */
static gchar *
fu_redfish_nvidia_device_find_reset_uri(FuRedfishBackend *backend, GError **error)
{
	g_autofree gchar *reset_uri = NULL;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);
	g_autoptr(FwupdJsonObject) json_chassis = NULL;

	if (!fu_redfish_request_perform(request,
					FU_REDFISH_NVIDIA_CHASSIS_URI,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return NULL;
	json_chassis = fu_redfish_request_get_json_object(request);
	if (json_chassis != NULL)
		reset_uri = fu_redfish_nvidia_device_chassis_reset_uri(json_chassis);
	if (reset_uri == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s does not advertise NvidiaChassis.AuxPowerReset",
			    FU_REDFISH_NVIDIA_CHASSIS_URI);
		return NULL;
	}
	return g_steal_pointer(&reset_uri);
}

static gboolean
fu_redfish_nvidia_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRedfishBackend *backend;
	glong status_code;
	g_autofree gchar *reset_uri = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GError) error_local = NULL;

	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;
	reset_uri = fu_redfish_nvidia_device_find_reset_uri(backend, error);
	if (reset_uri == NULL)
		return FALSE;

	/* AuxPowerCycleForce does not wait for the host to shut down and drops the
	 * rail powering this process, so flush the filesystems while we still can */
	sync();

	fwupd_json_object_add_string(json_obj, "ResetType", "AuxPowerCycleForce");
	request = fu_redfish_backend_request_new(backend);
	g_debug("requesting aux power cycle via %s", reset_uri);
	if (!fu_redfish_request_perform_full(request,
					     reset_uri,
					     "POST",
					     json_obj,
					     FU_REDFISH_REQUEST_PERFORM_FLAG_NONE,
					     &error_local)) {
		/* the BMC drops the rail as it acts on the request, so losing the
		 * connection before any status arrives is the expected outcome */
		if (fu_redfish_request_get_status_code(request) == 0) {
			g_debug("connection lost as the aux rail dropped: %s",
				error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		g_prefix_error_literal(error, "failed to request aux power cycle: ");
		return FALSE;
	}

	/* fu_redfish_request_perform() only rejects HTTP 401, so check the status here
	 * rather than report a rejected action as a successful activation */
	status_code = fu_redfish_request_get_status_code(request);
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
	/* a recovery registered without a function makes fu_device_retry_full() abort
	 * as soon as that error is returned; the task poll normalizes every failure it
	 * does not want retried into one of these two codes */
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_WRITE, NULL);
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED, NULL);

	/* write_firmware() explains the aux-rail power cycle in its own words */
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE);

	/* NEEDS_ACTIVATION is runtime-only, so without this any daemon restart
	 * between staging and activating would strand the staged firmware */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION);
}

static void
fu_redfish_nvidia_device_class_init(FuRedfishNvidiaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_redfish_nvidia_device_probe;
	device_class->prepare_firmware = fu_redfish_nvidia_device_prepare_firmware;
	device_class->write_firmware = fu_redfish_nvidia_device_write_firmware;
	device_class->activate = fu_redfish_nvidia_device_activate;
	device_class->set_progress = fu_redfish_nvidia_device_set_progress;
}
