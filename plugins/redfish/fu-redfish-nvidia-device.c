/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * FuRedfishNvidiaDevice — NVIDIA DGX Station GB300 OOB firmware update via Redfish.
 *
 * Represents a single component in the BMC's FirmwareInventory (e.g.
 * /redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0) and implements
 * the GB300-specific upload + task-monitoring flow:
 *
 *   1. Multipart POST to UpdateService/MultipartHttpPushUri:
 *        UpdateParameters: { "Targets":[], "ForceUpdate":true,
 *                            "@Redfish.OperationApplyTime":"Immediate" }
 *        UpdateFile: raw firmware blob
 *
 *   2. Poll /redfish/v1/TaskService/Tasks/<id> (the persistent task resource,
 *      not the /Monitor sub-resource) for PercentComplete and TaskState.
 *      GB300-specific divergences from DMTF DSP0266:
 *        - Targets must be an empty array (BMC resolves components from the
 *          PLDM bundle manifest); passing the logical_id causes a 400 error.
 *        - ForceUpdate=true is required; the BMC rejects same-version installs.
 *        - OperationApplyTime "OnReset" is not in the BMC's acceptable-values
 *          list; "Immediate" must be used.
 *        - PercentComplete is only updated on /Tasks/<id>, not on
 *          /Tasks/<id>/Monitor; polling the Monitor always returns 0%.
 *        - After task completion the BMC returns HTTP 200 + empty body for the
 *          TaskMonitor instead of the canonical 404 that signals reap; both
 *          are handled here. When both the Monitor and /Tasks/<id> are gone
 *          the resource-gone condition is treated as Completed.
 *
 *   3. attach() sets NEEDS_ACTIVATION; the firmware is staged and only becomes
 *      active after an aux-rail (AC) power cycle. The BMC's AuxPowerReset OEM
 *      action is gated on chassis-off so automation from the host is impossible.
 *
 * Instance IDs registered per device (in addition to those from the base class):
 *   NVIDIA_OOB\URI_<@odata.id>   — always; unique per FirmwareInventory entry
 *   NVIDIA_OOB\SWID_<SoftwareId> — when present; enables unified-image CABs to
 *                                   target multiple components by SoftwareId
 *
 * Backend detection: fu_redfish_backend_coldplug() sets device_gtype to
 * FU_TYPE_REDFISH_NVIDIA_DEVICE when the Redfish root Vendor equals "NVIDIA"
 * or when /redfish/v1/Chassis/Chassis_0 reports Manufacturer="NVIDIA" and
 * Model contains "GB300" and "Station".
 *
 * ── Prerequisites ─────────────────────────────────────────────────────────────
 *
 * This plugin consumes a Redfish X-Auth-Token written by the separate
 * nvidia-host-bmc-interface-setup platform package. That package must be
 * installed and commissioned before this plugin can enumerate devices.
 *
 * The token is read from:
 *   /run/nvidia-host-bmc-interface/session   (tmpfs, mode 600, root-only)
 *
 * If the file is absent fwupd falls back to Basic Auth (username/password
 * from /etc/fwupd/redfish.conf), which requires a plaintext password on disk.
 *
 * ── Build ─────────────────────────────────────────────────────────────────────
 *
 *   # Install build dependencies (Ubuntu/Debian)
 *   sudo apt install -y meson ninja-build pkg-config gettext \
 *       libusb-1.0-0-dev libglib2.0-dev libcurl4-openssl-dev \
 *       libjson-glib-dev libxmlb-dev libsqlite3-dev \
 *       libgnutls28-dev libgirepository1.0-dev netplan.io
 *
 *   # Upgrade meson if system version < 0.63.0
 *   pip3 install --user --break-system-packages "meson>=1.3.0"
 *   export PATH="$HOME/.local/bin:$PATH"
 *
 *   cd ~/fwupd
 *   meson setup build \
 *       -Dauto_features=disabled -Ddocs=disabled \
 *       -Dtests=false -Dman=false
 *   ninja -C build
 *
 * ── Install ───────────────────────────────────────────────────────────────────
 *
 *   sudo env PATH="/home/nvidia/.local/bin:$PATH" \
 *       PYTHONPATH="/home/nvidia/.local/lib/python3.12/site-packages" \
 *       meson install -C build
 *
 *   meson install places a systemd drop-in at
 *   /etc/systemd/system/fwupd.service.d/nvidia.conf that redirects
 *   fwupd.service to the newly installed binary and sets --verbose +
 *   G_MESSAGES_DEBUG=all so plugin log messages appear in journalctl.
 *
 *   After install, reload systemd and restart fwupd to pick up the new binary:
 *
 *   sudo systemctl daemon-reload
 *   sudo systemctl restart fwupd.service
 *   sleep 2
 *   systemctl status fwupd.service
 *
 * ── Verify ────────────────────────────────────────────────────────────────────
 *
 *   systemctl status fwupd.service      # confirm our binary is running
 *   fwupdmgr get-devices               # should list FW_BMC_0, FW_GPU_0, …
 *   sudo journalctl -u fwupd -f        # live plugin logs during install
 *
 * ── Firmware install ──────────────────────────────────────────────────────────
 *
 *   sudo fwupdmgr install firmware.cab --allow-reinstall
 *
 *   Progress is logged every 60 s:
 *     NvidiaOob: task poll #31 — 20% complete (~62 s elapsed)
 *   Typical install time for a full PLDM bundle: ~20 minutes.
 *   Firmware activates on the next AC (aux-rail) power cycle.
 */

#include "config.h"

#include <curl/curl.h>
#include <string.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-device.h"
#include "fu-redfish-nvidia-device.h"
#include "fu-redfish-request.h"

struct _FuRedfishNvidiaDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishNvidiaDevice, fu_redfish_nvidia_device, FU_TYPE_REDFISH_DEVICE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

/* ------------------------------------------------------------------ */
/* Location header capture (same pattern as FuRedfishMultipartDevice) */
/* ------------------------------------------------------------------ */

static size_t
fu_redfish_nvidia_device_location_header_cb(char *ptr,
					    size_t size,
					    size_t nmemb,
					    void *userdata)
{
	char **location = (char **)userdata;
	if ((size * nmemb) > 16 && g_ascii_strncasecmp(ptr, "Location:", 9) == 0)
		*location = g_strndup(ptr + 10, (size * nmemb) - 12);
	return size * nmemb;
}

/* ------------------------------------------------------------------ */
/* Task polling — GB300-specific                                       */
/* ------------------------------------------------------------------ */

/*
 * Derive the persistent /Tasks/<id> URI from a /Tasks/<id>/Monitor URI by
 * stripping the trailing /Monitor segment.  Returns NULL if uri does not end
 * in /Monitor (caller must check).
 */
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
	/* A reap status wins over any body the BMC happens to return. */
	if (status_code == 404)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;

	/* These statuses can clear while an update remains active. */
	if (status_code == 0 || status_code == 408 || status_code == 425 || status_code == 429 ||
	    status_code >= 500)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT;

	/* Never let task-shaped JSON turn an HTTP error into success. */
	if (status_code < 200 || status_code >= 300)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL;

	/* A 2xx response that failed transport or JSON validation is not a task. */
	if (!request_succeeded)
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL;

	if (!has_json) {
		/* This is a documented GB300 quirk of TaskMonitor only. */
		if (status_code == 200 && allow_empty_200)
			return FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED;
		return FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT;
	}

	return FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK;
}

static gboolean
fu_redfish_nvidia_device_task_state_is_running(const gchar *state)
{
	return g_strcmp0(state, "New") == 0 ||
	       g_strcmp0(state, "Starting") == 0 ||
	       g_strcmp0(state, "Running") == 0 ||
	       g_strcmp0(state, "Suspended") == 0 ||
	       g_strcmp0(state, "Pending") == 0 ||
	       g_strcmp0(state, "Stopping") == 0 ||
	       g_strcmp0(state, "Service") == 0 ||
	       g_strcmp0(state, "Cancelling") == 0 ||
	       g_strcmp0(state, "Continue") == 0;
}

/*
 * Parse TaskState and PercentComplete from a task JSON object.
 * Updates *out_percent (0–100).  On FAILED, also sets @error.
 */
FuRedfishNvidiaTaskState
fu_redfish_nvidia_device_parse_task(const gchar *task_uri,
				    FwupdJsonObject *json_task,
				    guint *out_percent,
				    GError **error)
{
	const gchar *state;
	const gchar *status;
	gint64 percent = 0;

	state = fwupd_json_object_get_string(json_task, "TaskState", NULL);
	status = fwupd_json_object_get_string(json_task, "TaskStatus", NULL);
	(void)fwupd_json_object_get_integer_with_default(json_task,
							 "PercentComplete",
							 &percent,
							 0,
							 NULL);
	if (out_percent != NULL)
		*out_percent = (guint)CLAMP(percent, 0, 100);

	if (state == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "BMC task %s has no TaskState",
			    task_uri);
		return FU_REDFISH_NVIDIA_TASK_FAILED;
	}

	/* TaskStatus=Critical is a fatal condition at any stage — per DMTF DSP0266
	 * the BMC sets this when the operation has failed, even if TaskState still
	 * shows Running.  Reject immediately so we do not wait for a state transition
	 * that will never arrive. */
	if (g_strcmp0(status, "Critical") == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "BMC task %s has TaskStatus=Critical (TaskState=%s)",
			    task_uri,
			    state != NULL ? state : "?");
		return FU_REDFISH_NVIDIA_TASK_FAILED;
	}

	/* Terminal failure states — task will not make further progress. */
	if (g_strcmp0(state, "Exception") == 0 ||
	    g_strcmp0(state, "Killed") == 0 ||
	    g_strcmp0(state, "Cancelled") == 0 ||
	    g_strcmp0(state, "UserIntervention") == 0 ||
	    g_strcmp0(state, "Interrupted") == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "BMC task %s failed (TaskState=%s)",
			    task_uri,
			    state != NULL ? state : "?");
		return FU_REDFISH_NVIDIA_TASK_FAILED;
	}

	if (g_strcmp0(state, "Completed") == 0)
		return FU_REDFISH_NVIDIA_TASK_COMPLETED;

	if (!fu_redfish_nvidia_device_task_state_is_running(state)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "BMC task %s has unknown TaskState=%s",
			    task_uri,
			    state);
		return FU_REDFISH_NVIDIA_TASK_FAILED;
	}

	/* Some GB300 BMC versions report 100% before changing a known running state. */
	if (percent == 100)
		return FU_REDFISH_NVIDIA_TASK_COMPLETED;

	return FU_REDFISH_NVIDIA_TASK_RUNNING;
}

static FuRedfishNvidiaTaskResponse
fu_redfish_nvidia_device_poll_request(FuRedfishBackend *backend,
				      const gchar *uri,
				      gboolean allow_empty_200,
				      FwupdJsonObject **out_json,
				      GError **error)
{
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);
	g_autoptr(FwupdJsonObject) json_task = NULL;
	g_autoptr(GError) error_local = NULL;
	FuRedfishNvidiaTaskResponse response;
	gboolean request_succeeded;
	glong status_code;

	g_return_val_if_fail(out_json != NULL, FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL);
	g_return_val_if_fail(*out_json == NULL, FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL);

	request_succeeded =
	    fu_redfish_request_perform(request,
				       uri,
				       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
				       &error_local);
	status_code = fu_redfish_request_get_status_code(request);
	json_task = fu_redfish_request_get_json_object(request);
	response = fu_redfish_nvidia_device_classify_task_response(status_code,
							    request_succeeded,
							    json_task != NULL,
							    allow_empty_200);

	if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK) {
		*out_json = g_steal_pointer(&json_task);
		return response;
	}
	if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED)
		return response;

	if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL &&
	    (status_code == 401 || status_code == 403)) {
		g_clear_error(&error_local);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "authentication failed with HTTP %li polling task %s",
			    status_code,
			    uri);
		return response;
	}
	if (error_local != NULL) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return response;
	}
	if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "transient HTTP %li polling task %s",
			    status_code,
			    uri);
		return response;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "unexpected HTTP %li polling task %s",
		    status_code,
		    uri);
	return response;
}

/*
 * Poll a Redfish TaskMonitor URI until the task completes, fails, or times out.
 *
 * Handles GB300-specific task lifecycle:
 *   - HTTP 200 + empty body signals TaskMonitor reaped (not a canonical 404);
 *     both are treated as "Monitor URI gone, try /Tasks/<id>".
 *   - If the persistent /Tasks/<id> is also gone after at least one successful
 *     poll, the BMC has completed the full PLDM activation lifecycle and reaped
 *     all task resources — treat as Completed.
 */
static gboolean
fu_redfish_nvidia_device_poll_task(FuRedfishNvidiaDevice *self,
				   FuRedfishBackend *backend,
				   const gchar *initial_location,
				   FuProgress *progress,
				   GError **error)
{
	/* On the GB300 BMC, PercentComplete is only updated on the persistent
	 * /Tasks/<id> resource, not on the /Tasks/<id>/Monitor sub-resource.
	 * Prefer the persistent URI for polling, but keep the monitor URI so
	 * we can detect its reap separately and use it as a fallback if the
	 * persistent URI was not yet created.  A newly accepted upload may
	 * need a few seconds before /Tasks/<id> appears. */
	g_autofree gchar *monitor_uri = g_strdup(initial_location);
	g_autofree gchar *persistent_uri =
	    fu_redfish_nvidia_device_derive_task_uri(initial_location);
	g_autofree gchar *task_uri =
	    persistent_uri ? g_steal_pointer(&persistent_uri)
			   : g_strdup(initial_location);
	/* Grace-period counter: allow N attempts before giving up on a task
	 * that has not appeared yet (persistent URI returns 404 immediately). */
	const guint task_creation_grace = 5;
	const guint poll_interval_ms = 2000;
	/* 1800 attempts × 2 s = 60 min, covering worst-case GB300 bundle flash. */
	const guint max_attempts = 1800;
	/* True only after the persistent /Tasks/<id> URI has returned at least
	 * one valid JSON body with a recognised TaskState.  Used to distinguish
	 * "task reaped after completing" from "task never existed".  The monitor
	 * URI returning a live response does NOT set this flag — only the
	 * persistent resource does. */
	gboolean saw_persistent_task = FALSE;
	/* Reset to 0 after each successful poll; abort only after this many
	 * consecutive failures to tolerate short network blips mid-update. */
	guint consecutive_errors = 0;

	g_message("NvidiaOob: polling task %s (up to %u s)",
		  task_uri, max_attempts * poll_interval_ms / 1000);

	for (guint attempt = 0; attempt < max_attempts; attempt++) {
		guint percent = 0;
		FuRedfishNvidiaTaskState ts;
		FuRedfishNvidiaTaskResponse response;
		g_autoptr(FwupdJsonObject) json_task = NULL;
		g_autoptr(GError) err_local = NULL;

		fu_device_sleep(FU_DEVICE(self), poll_interval_ms);

		response = fu_redfish_nvidia_device_poll_request(backend,
							 task_uri,
							 g_str_has_suffix(task_uri, "/Monitor"),
							 &json_task,
							 &err_local);
		if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK) {
			ts = fu_redfish_nvidia_device_parse_task(
			    task_uri, json_task, &percent, &err_local);
			fu_progress_set_percentage(progress, percent);
			if (ts == FU_REDFISH_NVIDIA_TASK_FAILED) {
				g_message("NvidiaOob: task failed at %s", task_uri);
				g_propagate_error(error, g_steal_pointer(&err_local));
				return FALSE;
			}
			if (!g_str_has_suffix(task_uri, "/Monitor"))
				saw_persistent_task = TRUE;
			consecutive_errors = 0;
			/* Log progress every 60 s. */
			if (attempt % 30 == 0)
				g_message("NvidiaOob: task poll #%u — %u%% complete "
					  "(~%u s elapsed)",
					  attempt + 1,
					  percent,
					  (attempt + 1) * poll_interval_ms / 1000);
			if (ts == FU_REDFISH_NVIDIA_TASK_COMPLETED) {
				g_message("NvidiaOob: task completed at %u%%", percent);
				fu_progress_set_percentage(progress, 100);
				return TRUE;
			}
			continue;
		}

		if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL) {
			g_propagate_error(error, g_steal_pointer(&err_local));
			return FALSE;
		}

		/* If the persistent task is gone, validate the monitor before declaring
		 * success.  A live monitor can still report a terminal failure. */
		if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED) {
			if (monitor_uri != NULL && !g_str_equal(task_uri, monitor_uri)) {
				g_autoptr(FwupdJsonObject) monitor_json = NULL;
				g_autoptr(GError) monitor_error = NULL;
				FuRedfishNvidiaTaskResponse monitor_response =
				    fu_redfish_nvidia_device_poll_request(backend,
								  monitor_uri,
								  TRUE,
								  &monitor_json,
								  &monitor_error);

				if (monitor_response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TASK) {
					guint monitor_percent = 0;
					FuRedfishNvidiaTaskState monitor_state =
					    fu_redfish_nvidia_device_parse_task(monitor_uri,
									monitor_json,
									&monitor_percent,
									&monitor_error);
					fu_progress_set_percentage(progress, monitor_percent);
					if (monitor_state == FU_REDFISH_NVIDIA_TASK_FAILED) {
						g_propagate_error(error,
								  g_steal_pointer(&monitor_error));
						return FALSE;
					}
					if (monitor_state == FU_REDFISH_NVIDIA_TASK_COMPLETED) {
						fu_progress_set_percentage(progress, 100);
						return TRUE;
					}
					consecutive_errors = 0;
					g_debug("NvidiaOob: monitor active, waiting for persistent task");
					continue;
				}
				if (monitor_response == FU_REDFISH_NVIDIA_TASK_RESPONSE_FATAL) {
					g_propagate_error(error, g_steal_pointer(&monitor_error));
					return FALSE;
				}
				if (monitor_response == FU_REDFISH_NVIDIA_TASK_RESPONSE_TRANSIENT) {
					g_clear_error(&err_local);
					err_local = g_steal_pointer(&monitor_error);
					response = monitor_response;
				} else if (saw_persistent_task) {
					g_message("NvidiaOob: task and monitor reaped after a valid poll — "
						  "treating as Completed");
					fu_progress_set_percentage(progress, 100);
					return TRUE;
				}
			} else if (saw_persistent_task) {
				g_message("NvidiaOob: task reaped after a valid poll — treating as Completed");
				fu_progress_set_percentage(progress, 100);
				return TRUE;
			}

			if (response == FU_REDFISH_NVIDIA_TASK_RESPONSE_REAPED) {
				if (attempt < task_creation_grace) {
					g_debug("NvidiaOob: task not yet visible (attempt %u/%u), retrying",
						attempt + 1,
						task_creation_grace);
					continue;
				}
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_FOUND,
						    "task not found after grace period; upload may not "
						    "have started or task was stale");
				return FALSE;
			}
		}

		/* Transient network blips: allow up to 5 consecutive failures so
		 * brief connectivity interruptions during a ~20 min update do not
		 * abort a still-running BMC flash.  The counter resets to 0 after
		 * each successful poll so intermittent errors are tolerated. */
		if (++consecutive_errors <= 5) {
			g_debug("transient poll error (consecutive %u): %s",
				consecutive_errors,
				err_local != NULL ? err_local->message : "unknown");
			continue;
		}

		if (err_local != NULL)
			g_propagate_error(error, g_steal_pointer(&err_local));
		else
			g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE,
					    "poll failed with no error detail");
		return FALSE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_TIMED_OUT,
		    "timed out waiting for BMC task %s after %u seconds",
		    task_uri,
		    max_attempts * poll_interval_ms / 1000);
	return FALSE;
}

/* ------------------------------------------------------------------ */
/* FuDevice vfuncs                                                     */
/* ------------------------------------------------------------------ */

static gboolean
fu_redfish_nvidia_device_probe(FuDevice *device, GError **error)
{
	FuRedfishDevice *rdev = FU_REDFISH_DEVICE(device);
	FwupdJsonObject *member;
	const gchar *inventory_uri;
	const gchar *software_id;

	/* Base probe: sets logical_id = @odata.id, vendor, version, name,
	 * SoftwareId instance IDs (REDFISH\VENDOR\SOFTWAREID), etc. */
	if (!FU_DEVICE_CLASS(fu_redfish_nvidia_device_parent_class)->probe(device, error))
		return FALSE;

	/* After base probe, logical_id holds the @odata.id inventory URI. */
	inventory_uri = fu_device_get_logical_id(device);
	if (inventory_uri == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "FirmwareInventory member has no @odata.id");
		return FALSE;
	}

	/*
	 * Override the device name with the inventory URI basename (e.g.
	 * FW_BMC_0, FW_GPU_0, FW_CPU_0).  The BMC's JSON "Name" field returns
	 * "Software Inventory" for every entry — identical for all 12+ OOB
	 * components — making each device indistinguishable by name.  The
	 * basename is unique, operator-meaningful, and matches LVFS CAB labels.
	 */
	{
		g_autofree gchar *basename = g_path_get_basename(inventory_uri);
		if (basename != NULL && basename[0] != '\0' &&
		    g_strcmp0(basename, ".") != 0 && g_strcmp0(basename, "/") != 0)
			fu_device_set_name(device, basename);
	}

	/*
	 * URI-derived instance ID — globally unique per FirmwareInventory entry.
	 * Primary match key for per-device LVFS CABs targeting a specific
	 * component.  The explicit add_guid() call registers the UUID-v5 hash
	 * immediately so the engine sees a GUID right after coldplug; the
	 * add_instance_id() call produces the display mapping in get-devices.
	 */
	{
		g_autofree gchar *iid =
		    g_strdup_printf("NVIDIA_OOB\\URI_%s", inventory_uri);
		fu_device_add_instance_id(device, iid);
	}

	/*
	 * SoftwareId-derived instance ID — allows a unified-image CAB to target
	 * multiple components that share a SoftwareId (e.g. FW_ERoT_CPU_0 and
	 * FW_ERoT_FPGA_0 both carry SoftwareId=0xFF00 in NVIDIA's PLDM bundle).
	 */
	member = fu_redfish_device_get_json_obj_member(rdev);
	if (member != NULL) {
		software_id = fwupd_json_object_get_string(member, "SoftwareId", NULL);
		if (software_id != NULL && software_id[0] != '\0') {
			g_autofree gchar *iid =
			    g_strdup_printf("NVIDIA_OOB\\SWID_%s", software_id);
			fu_device_add_instance_id(device, iid);
		}
	}

	/*
	 * Vendor IDs for LVFS CAB signature validation.  DMI:NVIDIA pairs with
	 * the GB300 system's SMBIOS identity; OEM:NVIDIA is the non-bus fallback
	 * since OOB-managed components have no PCI/USB/NVMe identity on the host.
	 */
	fu_device_add_vendor_id(device, "DMI:NVIDIA");
	fu_device_add_vendor_id(device, "OEM:NVIDIA");

	/*
	 * 15-minute install duration covers the worst-case scenario: full PLDM
	 * bundle flash across all components (BMC, SBIOS, GPU, CX, ERoT, MCU).
	 * An overestimate is far less confusing than an underestimate given that
	 * the BMC's PLDM verification + per-component fan-out runs ~10 minutes.
	 */
	/* GB300 PLDM bundle flash covers all components and takes ~20 minutes
	 * in practice; 45 minutes gives headroom for slower runs. */
	fu_device_set_install_duration(device, 2700);
	fu_device_set_summary(device, "OOB-managed firmware (activates on AC cycle)");

	return TRUE;
}

static FuFirmware *
fu_redfish_nvidia_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FuFirmwareParseFlags flags,
					  GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	gsize max_sz;
	gsize read_sz;

	/* Use the BMC-reported FirmwareSizeMax when available (the base class
	 * probe reads it from the UpdateService resource); otherwise use a finite
	 * 256 MiB ceiling to prevent runaway allocations on corrupt input. Read one
	 * byte beyond the limit so oversized payloads are rejected, never silently
	 * truncated to an apparently valid image. */
	max_sz = fu_device_get_firmware_size_max(device);
	if (max_sz == 0)
		max_sz = 256u * 1024u * 1024u;
	read_sz = max_sz < G_MAXSIZE ? max_sz + 1 : max_sz;
	blob = fu_input_stream_read_bytes(stream, 0, read_sz, progress, error);
	if (blob == NULL)
		return NULL;
	if (g_bytes_get_size(blob) > max_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware payload exceeds maximum size of %" G_GSIZE_FORMAT " bytes",
			    max_sz);
		return NULL;
	}
	if (g_bytes_get_size(blob) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware payload is empty");
		return NULL;
	}
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
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;
	g_autoptr(curl_mime) mime = NULL;

	/*
	 * Two sub-steps inside the "write" phase:
	 *   upload (5%)  — multipart POST; fast (~30 s for a ~117 MB bundle)
	 *   bmc-apply (95%) — PLDM verify + per-component fan-out (~10 min)
	 * Weighting 5/95 keeps fwupdmgr's rate-based ETA accurate: an ETA
	 * extrapolated at end-of-upload works out to ~10 min for the remaining
	 * 95%, matching install_duration.
	 */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "upload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 95, "bmc-apply");

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/*
	 * UpdateParameters JSON:
	 *   Targets: []          — empty; BMC resolves components from the PLDM
	 *                          bundle manifest.  Passing logical_id causes 400.
	 *   ForceUpdate: true    — required to bypass the BMC's same-version check.
	 *   OperationApplyTime:  — "Immediate" is the only accepted value; the BMC
	 *     "Immediate"          rejects "OnReset" with 400 InvalidValue.
	 */
	{
		g_autoptr(FwupdJsonObject) json_params = fwupd_json_object_new();
		g_autoptr(FwupdJsonArray) json_targets = fwupd_json_array_new();
		fwupd_json_object_add_array(json_params, "Targets", json_targets);
		fwupd_json_object_add_boolean(json_params, "ForceUpdate", TRUE);
		fwupd_json_object_add_string(json_params,
					     "@Redfish.OperationApplyTime",
					     "Immediate");
		params = fwupd_json_object_to_string(json_params, FWUPD_JSON_EXPORT_FLAG_INDENT);
	}

	/* Build the multipart POST. */
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
	/* 600 s upload timeout: ~117 MB over USB-OOB at ~1–2 MB/s. */
	(void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, (glong)600);

	g_message("NvidiaOob: uploading firmware (%.1f MB) to %s",
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
	g_message("NvidiaOob: upload accepted (HTTP 202) — waiting for TaskMonitor");

	/* Prefer Location: header; fall back to @odata.id in the response body. */
	if (location == NULL || location[0] == '\0') {
		FwupdJsonObject *json_resp = fu_redfish_request_get_json_object(request);
		if (!fwupd_json_object_has_node(json_resp, "@odata.id")) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no TaskMonitor location in upload response from %s",
				    fu_redfish_backend_get_push_uri_path(backend));
			return FALSE;
		}
		g_free(location);
		location =
		    g_strdup(fwupd_json_object_get_string(json_resp, "@odata.id", NULL));
	}
	g_message("NvidiaOob: TaskMonitor location: %s", location);
	fu_progress_step_done(progress); /* upload */

	if (!fu_redfish_nvidia_device_poll_task(FU_REDFISH_NVIDIA_DEVICE(device),
						backend,
						location,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress); /* bmc-apply */
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	/*
	 * GB300 OOB firmware is staged by the upload but only becomes active
	 * after an aux-rail (AC) power cycle.  Use fwupd's canonical activation
	 * flow: set NEEDS_ACTIVATION so `fwupdmgr activate` can be called, and
	 * document the manual procedure in update_message.
	 *
	 * We do NOT attempt to drive the cycle automatically:
	 *   - The BMC's NvidiaChassis.AuxPowerReset OEM action requires the
	 *     chassis to already be powered off.
	 *   - Issuing a chassis PowerOff kills this process before any follow-up
	 *     POST can fire.
	 * The activate() vfunc below surfaces this as NEEDS_USER_ACTION.
	 */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_update_message(
	    device,
	    "An AC (aux-rail) power cycle is required to activate the staged firmware:\n"
	    "  1. sudo poweroff\n"
	    "  2. After the host is fully off, physically unplug AC for at least 30 s\n"
	    "  3. Reconnect AC and power on — staged firmware will be active on next boot");
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	/*
	 * `fwupdmgr activate` dispatches here.  We cannot drive the AC cycle
	 * from the host (see attach() comment) so we return NEEDS_USER_ACTION
	 * with the full manual procedure.  fwupdmgr surfaces this as an error
	 * message; the user acts out of band.
	 */
	g_set_error_literal(
	    error,
	    FWUPD_ERROR,
	    FWUPD_ERROR_NEEDS_USER_ACTION,
	    "Activation cannot be performed automatically on this hardware.\n"
	    "An AC (aux-rail) power cycle is required and must be done out of band:\n"
	    "  1. sudo poweroff\n"
	    "  2. After the host is fully off, physically unplug AC for at least 30 s\n"
	    "  3. Reconnect AC and power on — staged firmware will be active on next boot.\n"
	    "Reason: the BMC's NvidiaChassis.AuxPowerReset OEM action is gated on "
	    "chassis-off, and the chassis-off step kills this process before a "
	    "follow-up POST can fire.");
	return FALSE;
}

static void
fu_redfish_nvidia_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING,  0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY,   0, "reload");
}

/* ------------------------------------------------------------------ */
/* GObject boilerplate                                                 */
/* ------------------------------------------------------------------ */

static void
fu_redfish_nvidia_device_init(FuRedfishNvidiaDevice *self)
{
}

static void
fu_redfish_nvidia_device_class_init(FuRedfishNvidiaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_redfish_nvidia_device_probe;
	device_class->prepare_firmware = fu_redfish_nvidia_device_prepare_firmware;
	device_class->write_firmware = fu_redfish_nvidia_device_write_firmware;
	device_class->attach = fu_redfish_nvidia_device_attach;
	device_class->activate = fu_redfish_nvidia_device_activate;
	device_class->set_progress = fu_redfish_nvidia_device_set_progress;
}
