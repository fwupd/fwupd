/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
fu_redfish_legacy_device_detach(FuDevice *dev, FuProgress *progress, GError **error)
{
	FuRedfishLegacyDevice *self = FU_REDFISH_LEGACY_DEVICE(dev);
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);
	g_autoptr(JsonBuilder) builder = json_builder_new();

	/* create header */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "HttpPushUriTargetsBusy");
	json_builder_add_boolean_value(builder, TRUE);
	json_builder_set_member_name(builder, "HttpPushUriTargets");
	json_builder_begin_array(builder);
	json_builder_add_string_value(builder, fu_device_get_logical_id(FU_DEVICE(self)));
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	/* patch the two fields */
	return fu_redfish_request_perform_full(request,
					       "/redfish/v1/UpdateService",
					       "PATCH",
					       builder,
					       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					       error);
}

static gboolean
fu_redfish_legacy_device_attach(FuDevice *dev, FuProgress *progress, GError **error)
{
	FuRedfishLegacyDevice *self = FU_REDFISH_LEGACY_DEVICE(dev);
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);
	g_autoptr(JsonBuilder) builder = json_builder_new();

	/* create header */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "HttpPushUriTargetsBusy");
	json_builder_add_boolean_value(builder, FALSE);
	json_builder_set_member_name(builder, "HttpPushUriTargets");
	json_builder_begin_array(builder);
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	/* patch the two fields */
	return fu_redfish_request_perform_full(request,
					       "/redfish/v1/UpdateService",
					       "PATCH",
					       builder,
					       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
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
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	CURL *curl;
	JsonObject *json_obj;
	const gchar *location;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* POST data */
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
	if (!json_object_has_member(json_obj, "@odata.id")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no task returned for %s",
			    fu_redfish_backend_get_push_uri_path(backend));
		return FALSE;
	}
	location = json_object_get_string_member(json_obj, "@odata.id");
	return fu_redfish_device_poll_task(FU_REDFISH_DEVICE(self), location, progress, error);
}

static void
fu_redfish_legacy_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_redfish_legacy_device_init(FuRedfishLegacyDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish legacy device");
}

static void
fu_redfish_legacy_device_class_init(FuRedfishLegacyDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->attach = fu_redfish_legacy_device_attach;
	klass_device->detach = fu_redfish_legacy_device_detach;
	klass_device->write_firmware = fu_redfish_legacy_device_write_firmware;
	klass_device->set_progress = fu_redfish_legacy_device_set_progress;
}
