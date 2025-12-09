/*
 * Copyright 2022 Kai Michaelis <kai.michaelis@immu.ne>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-device.h"
#include "fu-redfish-smc-device.h"

struct _FuRedfishSmcDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishSmcDevice, fu_redfish_smc_device, FU_TYPE_REDFISH_DEVICE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

static const gchar *
fu_redfish_smc_device_get_task(FwupdJsonObject *json_obj)
{
	const gchar *msgid;
	g_autoptr(FwupdJsonArray) json_array_messages = NULL;
	g_autoptr(FwupdJsonArray) json_array_msgargs = NULL;
	g_autoptr(FwupdJsonObject) json_object_accepted = NULL;
	g_autoptr(FwupdJsonObject) json_object_message = NULL;

	json_object_accepted = fwupd_json_object_get_object(json_obj, "Accepted", NULL);
	if (json_object_accepted == NULL)
		return NULL;
	json_array_messages =
	    fwupd_json_object_get_array(json_object_accepted, "@Message.ExtendedInfo", NULL);
	if (json_array_messages == NULL || fwupd_json_array_get_size(json_array_messages) != 1)
		return NULL;
	json_object_message = fwupd_json_array_get_object(json_array_messages, 0, NULL);
	if (json_object_message == NULL)
		return NULL;
	msgid = fwupd_json_object_get_string(json_object_message, "MessageId", NULL);
	if (g_strcmp0(msgid, "SMC.1.0.OemSimpleupdateAcceptedMessage") != 0)
		return NULL;
	json_array_msgargs = fwupd_json_object_get_array(json_object_message, "MessageArgs", NULL);
	if (json_array_msgargs == NULL)
		return NULL;
	if (fwupd_json_array_get_size(json_array_msgargs) != 1)
		return NULL;
	return fwupd_json_array_get_string(json_array_msgargs, 0, NULL);
}

static GString *
fu_redfish_smc_device_get_parameters(FuRedfishSmcDevice *self)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_array_targets = fwupd_json_array_new();
	g_autoptr(FwupdJsonObject) json_oem = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_oem_supermicro = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_oem_supermicro_bios = fwupd_json_object_new();

	/* create header */
	/* https://supermicro.com/manuals/other/RedishRefGuide.pdf */
	fwupd_json_array_add_string(json_array_targets, "/redfish/v1/Systems/1/Bios");
	fwupd_json_object_add_array(json_obj, "Targets", json_array_targets);

	fwupd_json_object_add_string(json_obj,
				     "@Redfish.OperationApplyTime",
				     "OnStartUpdateRequest");

	fwupd_json_object_add_boolean(json_oem_supermicro_bios, "PreserveME", TRUE);
	fwupd_json_object_add_boolean(json_oem_supermicro_bios, "PreserveNVRAM", TRUE);
	fwupd_json_object_add_boolean(json_oem_supermicro_bios, "PreserveSMBIOS", TRUE);
	fwupd_json_object_add_boolean(json_oem_supermicro_bios, "BackupBIOS", FALSE);

	fwupd_json_object_add_object(json_oem_supermicro, "BIOS", json_oem_supermicro_bios);
	fwupd_json_object_add_object(json_oem, "Supermicro", json_oem_supermicro);
	fwupd_json_object_add_object(json_obj, "Oem", json_oem);

	/* export as a string */
	return fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_NONE);
}

static gboolean
fu_redfish_smc_device_start_update(FuRedfishSmcDevice *self, FuProgress *progress, GError **error)
{
	FuRedfishBackend *backend;
	CURL *curl;
	const gchar *location = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(GError) error_local = NULL;

	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

	if (!fu_redfish_request_perform(
		request,
		"/redfish/v1/UpdateService/Actions/UpdateService.StartUpdate",
		FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
		&error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED))
			fu_device_add_problem(FU_DEVICE(self), FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	json_obj = fu_redfish_request_get_json_object(request);

	location = fu_redfish_smc_device_get_task(json_obj);
	if (location == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no task returned for %s",
			    fu_redfish_backend_get_push_uri_path(backend));
		return FALSE;
	}
	return fu_redfish_device_poll_task(FU_REDFISH_DEVICE(self), location, progress, error);
}

static gboolean
fu_redfish_smc_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuRedfishSmcDevice *self = FU_REDFISH_SMC_DEVICE(device);
	FuRedfishBackend *backend;
	CURL *curl;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	curl_mimepart *part;
	const gchar *location = NULL;
	g_autoptr(curl_mime) mime = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GString) params = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 50, "apply");

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* create the multipart for uploading the image request */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	mime = curl_mime_init(curl);

	params = fu_redfish_smc_device_get_parameters(self);
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateParameters");
	(void)curl_mime_type(part, "application/json");
	(void)curl_mime_data(part, params->str, CURL_ZERO_TERMINATED);
	g_debug("request: %s", params->str);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateFile");
	(void)curl_mime_type(part, "application/octet-stream");
	(void)curl_mime_filename(part, "firmware.bin");
	(void)curl_mime_data(part, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));

	(void)curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					&error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_ALREADY_PENDING))
			fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	if (fu_redfish_request_get_status_code(request) != 202) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to upload: %li",
			    fu_redfish_request_get_status_code(request));
		return FALSE;
	}
	json_obj = fu_redfish_request_get_json_object(request);

	/* poll the verify task for progress */
	location = fu_redfish_smc_device_get_task(json_obj);
	if (location == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no task returned for %s",
			    fu_redfish_backend_get_push_uri_path(backend));
		return FALSE;
	}

	if (!fu_redfish_device_poll_task(FU_REDFISH_DEVICE(self),
					 location,
					 fu_progress_get_child(progress),
					 error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_redfish_smc_device_start_update(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);
	return TRUE;
}

static void
fu_redfish_smc_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_redfish_smc_device_init(FuRedfishSmcDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish Supermicro device");
}

static void
fu_redfish_smc_device_class_init(FuRedfishSmcDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_redfish_smc_device_write_firmware;
	device_class->set_progress = fu_redfish_smc_device_set_progress;
}
