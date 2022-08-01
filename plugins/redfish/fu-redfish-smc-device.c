/*
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-smc-device.h"

struct _FuRedfishSmcDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishSmcDevice, fu_redfish_smc_device, FU_TYPE_REDFISH_DEVICE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

static const gchar *
fu_redfish_smc_device_get_task(JsonObject *json_obj)
{
	JsonObject *tmp_obj;
	JsonArray *tmp_ary;
	const gchar *msgid;

	if (!json_object_has_member(json_obj, "Accepted"))
		return NULL;
	tmp_obj = json_object_get_object_member(json_obj, "Accepted");
	if (!json_object_has_member(tmp_obj, "@Message.ExtendedInfo"))
		return NULL;
	tmp_ary = json_object_get_array_member(tmp_obj, "@Message.ExtendedInfo");
	if (json_array_get_length(tmp_ary) != 1)
		return NULL;
	tmp_obj = json_array_get_object_element(tmp_ary, 0);
	msgid = json_object_get_string_member(tmp_obj, "MessageId");
	if (g_strcmp0(msgid, "SMC.1.0.OemSimpleupdateAcceptedMessage") != 0)
		return NULL;
	if (!(tmp_ary = json_object_get_array_member(tmp_obj, "MessageArgs")))
		return NULL;
	if (json_array_get_length(tmp_ary) != 1)
		return NULL;
	return json_array_get_string_element(tmp_ary, 0);
}

static GString *
fu_redfish_smc_device_get_parameters(FuRedfishSmcDevice *self)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = json_generator_new();
	g_autoptr(JsonNode) json_root = NULL;

	/* create header */
	/* https://supermicro.com/manuals/other/RedishRefGuide.pdf */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Targets");
	json_builder_begin_array(builder);
	json_builder_add_string_value(builder, "/redfish/v1/Systems/1/Bios");
	json_builder_end_array(builder);
	json_builder_set_member_name(builder, "@Redfish.OperationApplyTime");
	json_builder_add_string_value(builder, "OnStartUpdateRequest");
	json_builder_set_member_name(builder, "Oem");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Supermicro");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "BIOS");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "PreserveME");
	json_builder_add_boolean_value(builder, TRUE);
	json_builder_set_member_name(builder, "PreserveNVRAM");
	json_builder_add_boolean_value(builder, TRUE);
	json_builder_set_member_name(builder, "PreserveSMBIOS");
	json_builder_add_boolean_value(builder, TRUE);
	json_builder_set_member_name(builder, "BackupBIOS");
	json_builder_add_boolean_value(builder, FALSE);
	json_builder_end_object(builder);
	json_builder_end_object(builder);
	json_builder_end_object(builder);
	json_builder_end_object(builder);

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	json_generator_to_gstring(json_generator, str);

	return g_steal_pointer(&str);
}

static gboolean
fu_redfish_smc_device_start_update(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(device));
	JsonObject *json_obj;
	CURL *curl;
	const gchar *location = NULL;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);

	curl = fu_redfish_request_get_curl(request);
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;
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
	return fu_redfish_device_poll_task(FU_REDFISH_DEVICE(device), location, progress, error);
}

static gboolean
fu_redfish_smc_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuRedfishSmcDevice *self = FU_REDFISH_SMC_DEVICE(device);
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	CURL *curl;
	JsonObject *json_obj;
	curl_mimepart *part;
	const gchar *location = NULL;
	g_autoptr(curl_mime) mime = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* create the multipart for uploading the image request */
	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	mime = curl_mime_init(curl);
	(void)curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

	params = fu_redfish_smc_device_get_parameters(self);
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateParameters");
	(void)curl_mime_type(part, "application/json");
	(void)curl_mime_data(part, params->str, CURL_ZERO_TERMINATED);
	if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL)
		g_debug("request: %s", params->str);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "UpdateFile");
	(void)curl_mime_type(part, "application/octet-stream");
	(void)curl_mime_filedata(part, "firmware.bin");
	(void)curl_mime_data(part, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));

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
	if (!fu_redfish_device_poll_task(FU_REDFISH_DEVICE(self), location, progress, error))
		return FALSE;

	return fu_redfish_smc_device_start_update(device, progress, error);
}

static void
fu_redfish_smc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 50, "restart");
}

static void
fu_redfish_smc_device_init(FuRedfishSmcDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish Supermicro device");
}

static void
fu_redfish_smc_device_class_init(FuRedfishSmcDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_redfish_smc_device_write_firmware;
	klass_device->set_progress = fu_redfish_smc_device_set_progress;
}
