/*
 * Copyright 2024 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-device.h"
#include "fu-redfish-nvidia-device.h"
#include "fu-redfish-request.h"

struct _FuRedfishNvidiaDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishNvidiaDevice, fu_redfish_nvidia_device, FU_TYPE_REDFISH_DEVICE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

/* build UpdateParameters with empty targets so PLDM matches by component descriptor only */
static GString *
fu_redfish_nvidia_device_get_parameters(FuRedfishNvidiaDevice *self)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	/* empty Targets array — BMC resolves target from PLDM package descriptors */
	fwupd_json_object_add_array(json_obj, "Targets", json_arr);
	/* always force update for NVIDIA Redfish updates */
	fwupd_json_object_add_boolean(json_obj, "ForceUpdate", TRUE);
	fwupd_json_object_add_string(json_obj, "@Redfish.OperationApplyTime", "Immediate");

	return fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);
}

static gboolean
fu_redfish_nvidia_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuRedfishNvidiaDevice *self = FU_REDFISH_NVIDIA_DEVICE(device);
	FuRedfishBackend *backend;
	CURL *curl;
	curl_mimepart *part;
	g_autofree gchar *location = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(curl_mime) mime = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;

	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	mime = curl_mime_init(curl);

	/* part 1: UpdateParameters JSON with empty Targets and ForceUpdate=true */
	params = fu_redfish_nvidia_device_get_parameters(self);
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateParameters");
	(void)curl_mime_type(part, "application/json");
	(void)curl_mime_data(part, params->str, CURL_ZERO_TERMINATED);
	g_message("nvidia-oob: UpdateParameters JSON: %s", params->str);

	/* part 2: firmware binary */
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateFile");
	(void)curl_mime_type(part, "application/octet-stream");
	(void)curl_mime_filename(part, fu_firmware_get_filename(firmware));
	(void)curl_mime_data(part, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));

	(void)curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
	/* 360s for the firmware upload itself — large file transfer */
	(void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, (glong)360);

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_percentage(progress, 5);

	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	if (fu_redfish_request_get_status_code(request) != 202) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "upload rejected with HTTP %li",
			    fu_redfish_request_get_status_code(request));
		return FALSE;
	}

	/* get task location from @odata.id in response body */
	json_obj = fu_redfish_request_get_json_object(request);
	if (!fwupd_json_object_has_node(json_obj, "@odata.id")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no task location returned");
		return FALSE;
	}
	location = g_strdup(fwupd_json_object_get_string(json_obj, "@odata.id", error));
	if (location == NULL)
		return FALSE;
	/* task polling should be shown as verify stage */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_redfish_nvidia_device_poll_task(self, location, progress, error))
		return FALSE;

	/* firmware staged in BMC; explicit activation guidance for CLI and UI */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_update_message(device,
				     "Activation cannot be performed automatically on this hardware.\n\n"
				     "BIOS-only firmware updates may require a host reboot or AC cycle.\n"
				     "BMC, Bundle firmware updates require an AC cycle.\n\n"
				     "An AC (aux-rail) power cycle must be done out of band:\n"
				     "  1. sudo poweroff\n"
				     "  2. After the host is fully off, physically unplug AC for at least 30 seconds\n"
				     "  3. Reconnect AC and power on; staged firmware will be active on next boot.");
	return TRUE;
}

/* build a human-readable failure detail from Messages array or TaskStatus fallback */
static gchar *
fu_redfish_nvidia_device_task_failure_detail(FwupdJsonObject *json_obj)
{
	g_autoptr(FwupdJsonArray) json_msgs = NULL;
	g_autoptr(GString) detail = g_string_new(NULL);

	json_msgs = fwupd_json_object_get_array(json_obj, "Messages", NULL);
	if (json_msgs != NULL) {
		for (guint i = 0; i < fwupd_json_array_get_size(json_msgs); i++) {
			g_autoptr(FwupdJsonObject) json_msg = NULL;
			const gchar *text;

			json_msg = fwupd_json_array_get_object(json_msgs, i, NULL);
			if (json_msg == NULL)
				continue;
			text = fwupd_json_object_get_string(json_msg, "Message", NULL);
			if (text != NULL) {
				if (detail->len > 0)
					g_string_append(detail, "; ");
				g_string_append(detail, text);
			}
		}
	}

	/* fall back to TaskStatus when Messages is absent */
	if (detail->len == 0) {
		const gchar *task_status =
		    fwupd_json_object_get_string(json_obj, "TaskStatus", NULL);
		g_string_assign(detail, task_status != NULL ? task_status : "unknown");
	}

	return g_string_free(g_steal_pointer(&detail), FALSE);
}

/* poll the task URI directly every 2 seconds for up to 30 minutes; tolerates transient network errors */
gboolean
fu_redfish_nvidia_device_poll_task(FuRedfishNvidiaDevice *self,
				   const gchar *task_uri,
				   FuProgress *progress,
				   GError **error)
{
	const guint poll_interval_ms = 1000;
	const guint max_attempts = 1800; /* 1800 × 1 s = 30 mins */
	guint consecutive_errors = 0;

	for (guint i = 0; i < max_attempts; i++) {
		FuRedfishBackend *backend;
		const gchar *state_tmp;
		gint64 pc = 0;
		g_autofree gchar *detail = NULL;
		g_autoptr(FuRedfishRequest) request = NULL;
		g_autoptr(FwupdJsonObject) json_obj = NULL;
		g_autoptr(GError) error_local = NULL;

		fu_device_sleep(FU_DEVICE(self), poll_interval_ms); /* ms */

		backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
		if (backend == NULL)
			return FALSE;

		request = fu_redfish_backend_request_new(backend);
		/* status polls should fail reasonably fast without waiting too long */
		(void)curl_easy_setopt(fu_redfish_request_get_curl(request), CURLOPT_TIMEOUT, (glong)35);
		if (!fu_redfish_request_perform(request,
						task_uri,
						FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
						&error_local)) {
			/* tolerate up to 5 consecutive network hiccups mid-update */
			if (++consecutive_errors <= 5) {
				g_debug("transient poll error (%u/5): %s",
					consecutive_errors,
					error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		consecutive_errors = 0;

		json_obj = fu_redfish_request_get_json_object(request);
		if (!fwupd_json_object_get_integer_with_default(json_obj,
								"PercentComplete",
								&pc,
								-1,
								NULL))
			pc = -1;
		if (pc >= 0 && pc <= 100)
			fu_progress_set_percentage(progress, (guint)pc);

		state_tmp = fwupd_json_object_get_string(json_obj, "TaskState", NULL);
		if (state_tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "no TaskState in poll response");
			return FALSE;
		}
		g_debug("polling %s: TaskState=%s (%" G_GINT64_FORMAT "%%)",
			task_uri,
			state_tmp,
			pc);

		if (g_strcmp0(state_tmp, "Completed") == 0) {
			fu_progress_set_percentage(progress, 100);
			return TRUE;
		}

		if (g_strcmp0(state_tmp, "Exception") == 0 ||
		    g_strcmp0(state_tmp, "Cancelled") == 0 ||
			g_strcmp0(state_tmp, "Killed") == 0 ||
		    g_strcmp0(state_tmp, "UserIntervention") == 0) {
			detail = fu_redfish_nvidia_device_task_failure_detail(json_obj);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "BMC task %s failed: TaskState=%s, detail: %s",
				    task_uri,
				    state_tmp,
				    detail);
			return FALSE;
		}
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_TIMED_OUT,
		    "timed out waiting for BMC task %s after %u seconds",
		    task_uri,
		    max_attempts * poll_interval_ms / 1000);
	return FALSE;
}

static gboolean
fu_redfish_nvidia_device_probe(FuDevice *device, GError **error)
{
	FuDeviceClass *parent_class =
	    FU_DEVICE_CLASS(fu_redfish_nvidia_device_parent_class);
	const gchar *name;

	/* run standard Redfish probe: sets logical_id, backend_id, vendor, version, flags */
	if (!parent_class->probe(device, error))
		return FALSE;

	/* skip non-updatable inventory entries so they are invisible to fwupdmgr */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				    "not updatable");
		return FALSE;
	}

	fu_device_set_install_duration(device, 900);

	/* override Description-derived summary set by the parent probe */
	fu_device_set_summary(device, "OOB-managed firmware (activates on AC cycle)");

	/* avoid generic inventory naming when Redfish provides a stable inventory Id. */
	name = fu_device_get_name(device);
	if (g_strcmp0(name, "Software Inventory") == 0 &&
	    fu_device_get_backend_id(device) != NULL)
		fu_device_set_name(device, fu_device_get_backend_id(device));

	/* if SoftwareId was present the parent already built a GUID; skip */
	if (fu_device_get_instance_ids(device)->len > 0)
		return TRUE;

	/* derive a stable GUID from REDFISH\VENDOR_<vendor>&ID_<Id> for inventory
	 * entries that carry no SoftwareId (common on NVIDIA/OpenBMC platforms) */
	if (fu_device_get_backend_id(device) != NULL &&
	    !fu_device_build_instance_id(device, error, "REDFISH", "VENDOR", "ID", NULL))
		return FALSE;

	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	/* show activation guidance during `fwupdmgr activate`. */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REPLUG_POWER);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_set_message(request,
				  "Activation cannot be performed automatically on this hardware.\n\n"
				  "BIOS-only firmware updates may require a host reboot or AC cycle.\n"
				  "BMC, Bundle firmware updates require an AC cycle.\n\n"
				  "An AC (aux-rail) power cycle must be done out of band:\n"
				  "  1. sudo poweroff\n"
				  "  2. After the host is fully off, physically unplug AC for at least 30 seconds\n"
				  "  3. Reconnect AC and power on; staged firmware will be active on next boot.");
	if (!fu_device_emit_request(device, request, progress, error)) {
		g_prefix_error_literal(error, "failed to emit activation guidance: ");
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
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
}

static void
fu_redfish_nvidia_device_class_init(FuRedfishNvidiaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_redfish_nvidia_device_probe;
	device_class->activate = fu_redfish_nvidia_device_activate;
	device_class->write_firmware = fu_redfish_nvidia_device_write_firmware;
	device_class->set_progress = fu_redfish_nvidia_device_set_progress;
}
