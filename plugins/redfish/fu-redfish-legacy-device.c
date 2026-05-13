/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>

#include "fu-redfish-legacy-device.h"
#include "fu-redfish-request.h"

struct _FuRedfishLegacyDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishLegacyDevice, fu_redfish_legacy_device, FU_TYPE_REDFISH_DEVICE)

static gboolean
fu_redfish_legacy_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRedfishBackend *backend;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	/* sanity check */
	if (fu_device_get_logical_id(device) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no HttpPushUriTargets defined");
		return FALSE;
	}

	/* create header */
	fwupd_json_object_add_boolean(json_obj, "HttpPushUriTargetsBusy", TRUE);
	fwupd_json_array_add_string(json_arr, fu_device_get_logical_id(device));
	fwupd_json_object_add_array(json_obj, "HttpPushUriTargets", json_arr);

	/* patch the two fields */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	return fu_redfish_request_perform_full(request,
					       "/redfish/v1/UpdateService",
					       "PATCH",
					       json_obj,
					       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
						   FU_REDFISH_REQUEST_PERFORM_FLAG_USE_ETAG,
					       error);
}

static gboolean
fu_redfish_legacy_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRedfishBackend *backend;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	/* create header */
	fwupd_json_object_add_boolean(json_obj, "HttpPushUriTargetsBusy", FALSE);
	fwupd_json_object_add_array(json_obj, "HttpPushUriTargets", json_arr);

	/* patch the two fields */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	return fu_redfish_request_perform_full(request,
					       "/redfish/v1/UpdateService",
					       "PATCH",
					       json_obj,
					       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
						   FU_REDFISH_REQUEST_PERFORM_FLAG_USE_ETAG,
					       error);
}

static gboolean
fu_redfish_legacy_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuRedfishLegacyDevice *self = FU_REDFISH_LEGACY_DEVICE(device);
	FuRedfishBackend *backend;
	CURL *curl;
	const gchar *location;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* POST data */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	(void)curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, g_bytes_get_data(fw, NULL));
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)g_bytes_get_size(fw));
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	/* poll the task for progress */
	json_obj = fu_redfish_request_get_json_object(request);
	location = fwupd_json_object_get_string(json_obj, "@odata.id", error);
	if (location == NULL) {
		g_prefix_error(error,
			       "no task returned for %s: ",
			       fu_redfish_backend_get_push_uri_path(backend));
		return FALSE;
	}
	return fu_redfish_device_poll_task(FU_REDFISH_DEVICE(self), location, progress, error);
}

static void
fu_redfish_legacy_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_redfish_legacy_device_init(FuRedfishLegacyDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish legacy device");
}

static void
fu_redfish_legacy_device_class_init(FuRedfishLegacyDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_redfish_legacy_device_attach;
	device_class->detach = fu_redfish_legacy_device_detach;
	device_class->write_firmware = fu_redfish_legacy_device_write_firmware;
	device_class->set_progress = fu_redfish_legacy_device_set_progress;
}
