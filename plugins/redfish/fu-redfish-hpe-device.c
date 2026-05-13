/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2025 Arno Dubois <arno.du@orange.fr>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-hpe-device.h"
#include "fu-redfish-request.h"

struct _FuRedfishHpeDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishHpeDevice, fu_redfish_hpe_device, FU_TYPE_REDFISH_DEVICE)

typedef struct curl_slist _curl_slist;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(_curl_slist, curl_slist_free_all)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

static gboolean
fu_redfish_hpe_device_attach(FuDevice *dev, FuProgress *progress, GError **error)
{
	FuRedfishHpeDevice *self = FU_REDFISH_HPE_DEVICE(dev);
	FuRedfishBackend *backend;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonObject) json_oem = NULL;

	/* create URI and poll */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	if (!fu_redfish_request_perform(request,
					"/redfish/v1/UpdateService",
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	/* percentage is optional */
	json_obj = fu_redfish_request_get_json_object(request);
	json_oem = fwupd_json_object_get_object(json_obj, "Oem", NULL);
	if (json_oem != NULL) {
		g_autoptr(FwupdJsonObject) json_oem_hpe = NULL;
		json_oem_hpe = fwupd_json_object_get_object(json_oem, "Hpe", NULL);
		if (json_oem_hpe != NULL) {
			const gchar *status =
			    fwupd_json_object_get_string(json_oem_hpe, "State", NULL);

			/* if we are in an idle-ish state, we can proceed */
			if (g_strcmp0(status, "Idle") == 0 || g_strcmp0(status, "Error") == 0 ||
			    g_strcmp0(status, "Complete") == 0) {
				return TRUE;
			}
			g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device is busy");
			return FALSE;
		}
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unknown failure");
	return FALSE;
}

typedef struct {
	gboolean completed;
	FuProgress *progress;
} FuRedfishHpeDevicePollCtx;

static FuRedfishHpeDevicePollCtx *
fu_redfish_hpe_device_poll_ctx_new(FuProgress *progress)
{
	FuRedfishHpeDevicePollCtx *ctx = g_new0(FuRedfishHpeDevicePollCtx, 1);
	ctx->progress = g_object_ref(progress);
	return ctx;
}

static void
fu_redfish_hpe_device_poll_ctx_free(FuRedfishHpeDevicePollCtx *ctx)
{
	g_object_unref(ctx->progress);
	g_free(ctx);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuRedfishHpeDevicePollCtx, fu_redfish_hpe_device_poll_ctx_free)
#pragma clang diagnostic pop

static gboolean
fu_redfish_hpe_device_poll_task_once(FuRedfishDevice *self,
				     FuRedfishHpeDevicePollCtx *ctx,
				     GError **error)
{
	FuRedfishBackend *backend;
	const gchar *status;
	gint64 pc = 0;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_oem = NULL;
	g_autoptr(FwupdJsonObject) json_oem_hpe = NULL;

	/* create URI and poll */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend);
	if (!fu_redfish_request_perform(request,
					"/redfish/v1/UpdateService",
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	/* percentage is optional */
	json_obj = fu_redfish_request_get_json_object(request);

	json_oem = fwupd_json_object_get_object(json_obj, "Oem", NULL);
	if (json_oem == NULL)
		return TRUE;
	json_oem_hpe = fwupd_json_object_get_object(json_oem, "Hpe", NULL);
	if (json_oem_hpe == NULL)
		return TRUE;

	status = fwupd_json_object_get_string(json_oem_hpe, "State", NULL);
	if (g_strcmp0(status, "Error") == 0) {
		const gchar *message = NULL;
		g_autoptr(FwupdJsonObject) json_result = NULL;
		/* default error, will be replaced by something more fitting
		 * in fu_redfish_device_parse_message_id if we can
		 */
		json_result = fwupd_json_object_get_object(json_oem_hpe, "Result", NULL);
		if (json_result != NULL) {
			const gchar *message_id;
			message_id = fwupd_json_object_get_string(json_result, "MessageId", NULL);
			message = fwupd_json_object_get_string(json_result, "Message", NULL);
			g_debug("message [%s]: %s", message_id, message);
			if (!fu_redfish_device_parse_message_id(self,
								message_id,
								message,
								ctx->progress,
								error))
				return FALSE;
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    message != NULL ? message : "Unknown failure");
		return FALSE;
	}
	if (!fwupd_json_object_get_integer_with_default(json_oem_hpe,
							"FlashProgressPercent",
							&pc,
							-1,
							error))
		return FALSE;
	if (pc >= 0 && pc <= 100)
		fu_progress_set_percentage(ctx->progress, (guint)pc);

	if (g_strcmp0(status, "Writing") == 0 || g_strcmp0(status, "Updating") == 0) {
		fu_progress_set_status(ctx->progress, FWUPD_STATUS_DEVICE_WRITE);
	} else if (g_strcmp0(status, "Verifying") == 0) {
		fu_progress_set_status(ctx->progress, FWUPD_STATUS_DEVICE_VERIFY);
	} else if (g_strcmp0(status, "Complete") == 0) {
		ctx->completed = TRUE;
		fu_progress_set_status(ctx->progress, FWUPD_STATUS_IDLE);
	}

	/* try again */
	return TRUE;
}

static gboolean
fu_redfish_hpe_device_poll_task(FuRedfishDevice *self, FuProgress *progress, GError **error)
{
	const guint timeout = 2400;
	g_autoptr(FuRedfishHpeDevicePollCtx) ctx = fu_redfish_hpe_device_poll_ctx_new(progress);
	g_autoptr(GTimer) timer = g_timer_new();

	/* sleep and then reprobe hardware */
	do {
		fu_device_sleep(FU_DEVICE(self), 1000); /* ms */
		if (!fu_redfish_hpe_device_poll_task_once(self, ctx, error))
			return FALSE;
		if (ctx->completed) {
			fu_progress_finished(progress);
			return TRUE;
		}
	} while (g_timer_elapsed(timer, NULL) < timeout);

	/* success */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "failed to poll for success after %u seconds",
		    timeout);
	return FALSE;
}

static gboolean
fu_redfish_hpe_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuRedfishHpeDevice *self = FU_REDFISH_HPE_DEVICE(device);
	FuRedfishBackend *backend;
	CURL *curl;
	const gchar *sessionkey;
	curl_mimepart *part;
	g_autofree gchar *sessionkey_kv = NULL;
	g_autoptr(curl_mime) mime = NULL;
	g_autoptr(_curl_slist) hs = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) json_str = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_WAITING_FOR_AUTH, 3, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 82, NULL);

	/* create session */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	if (!fu_redfish_backend_create_session(backend, error))
		return FALSE;
	sessionkey = fu_redfish_backend_get_session_key(backend);
	fu_progress_step_done(progress);

	/* create the multipart request */
	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	mime = curl_mime_init(curl);

	/* create header */
	fwupd_json_object_add_boolean(json_obj, "UpdateRepository", FALSE);
	fwupd_json_object_add_boolean(json_obj, "UpdateTarget", TRUE);
	fwupd_json_object_add_string(json_obj, "ETag", "atag");

	/* export as a string */
	json_str = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "sessionKey");
	(void)curl_mime_data(part, sessionkey, CURL_ZERO_TERMINATED);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "parameters");
	(void)curl_mime_type(part, "application/json");
	(void)curl_mime_data(part, json_str->str, CURL_ZERO_TERMINATED);
	g_debug("request: %s", json_str->str);

	part = curl_mime_addpart(mime);
	curl_mime_name(part, "files[]");
	(void)curl_mime_type(part, "application/octet-stream");
	(void)curl_mime_filename(part, "firmware.fwpkg");
	(void)curl_mime_data(part, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));

	(void)curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

	sessionkey_kv = g_strconcat("sessionKey=", sessionkey, NULL);
	(void)curl_easy_setopt(curl, CURLOPT_COOKIE, sessionkey_kv);

	hs = curl_slist_append(hs, g_strconcat("X-Auth-Token: ", sessionkey, NULL));
	(void)curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hs);
	fu_progress_step_done(progress);

	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					0,
					error))
		return FALSE;
	if (fu_redfish_request_get_status_code(request) != 200) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to upload using HPE specific method: %li",
			    fu_redfish_request_get_status_code(request));
		return FALSE;
	}

	if (!fu_redfish_hpe_device_poll_task(FU_REDFISH_DEVICE(self),
					     fu_progress_get_child(progress),
					     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_redfish_hpe_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 3, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_redfish_hpe_device_init(FuRedfishHpeDevice *self)
{
}

static void
fu_redfish_hpe_device_class_init(FuRedfishHpeDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_redfish_hpe_device_attach;
	device_class->write_firmware = fu_redfish_hpe_device_write_firmware;
	device_class->set_progress = fu_redfish_hpe_device_set_progress;
}
