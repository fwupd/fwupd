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
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	JsonObject *json_obj;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);

	/* create URI and poll */
	if (!fu_redfish_request_perform(request,
					"/redfish/v1/UpdateService",
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	/* percentage is optional */
	json_obj = fu_redfish_request_get_json_object(request);
	if (json_object_has_member(json_obj, "Oem")) {
		JsonObject *oem = json_object_get_object_member(json_obj, "Oem");
		if (oem != NULL && json_object_has_member(oem, "Hpe")) {
			JsonObject *hpe = json_object_get_object_member(oem, "Hpe");
			const gchar *status = json_object_get_string_member(hpe, "State");

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
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	JsonObject *json_obj;
	const gchar *message = "Unknown failure";
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(backend);

	/* create URI and poll */
	if (!fu_redfish_request_perform(request,
					"/redfish/v1/UpdateService",
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	/* percentage is optional */
	json_obj = fu_redfish_request_get_json_object(request);
	if (json_object_has_member(json_obj, "Oem")) {
		JsonObject *oem = json_object_get_object_member(json_obj, "Oem");
		if (oem != NULL && json_object_has_member(oem, "Hpe")) {
			JsonObject *hpe = json_object_get_object_member(oem, "Hpe");

			const gchar *status = json_object_get_string_member(hpe, "State");
			if (g_strcmp0(status, "Error") == 0) {
				/* default error, will be replaced by something more fitting
				 * in fu_redfish_device_parse_message_id if we can
				 */
				if (json_object_has_member(hpe, "Result")) {
					JsonObject *result =
					    json_object_get_object_member(hpe, "Result");
					const gchar *message_id = NULL;

					if (json_object_has_member(result, "MessageId"))
						message_id =
						    json_object_get_string_member(result,
										  "MessageId");
					if (json_object_has_member(result, "Message"))
						message = json_object_get_string_member(result,
											"Message");
					/* use the message */
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
						    message);
				return FALSE;
			}

			if (json_object_has_member(hpe, "FlashProgressPercent")) {
				gint64 pc = json_object_get_int_member(hpe, "FlashProgressPercent");
				if (pc >= 0 && pc <= 100)
					fu_progress_set_percentage(ctx->progress, (guint)pc);
			}

			if (g_strcmp0(status, "Writing") == 0 ||
			    g_strcmp0(status, "Updating") == 0) {
				fu_progress_set_status(ctx->progress, FWUPD_STATUS_DEVICE_WRITE);
			} else if (g_strcmp0(status, "Verifying") == 0) {
				fu_progress_set_status(ctx->progress, FWUPD_STATUS_DEVICE_VERIFY);
			} else if (g_strcmp0(status, "Complete") == 0) {
				ctx->completed = TRUE;
				fu_progress_set_status(ctx->progress, FWUPD_STATUS_IDLE);
			}
		}
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
	FuRedfishBackend *backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self));
	CURL *curl;
	const gchar *sessionkey;
	curl_mimepart *part;
	g_autofree gchar *sessionkey_kv = NULL;
	g_autoptr(curl_mime) mime = NULL;
	g_autoptr(_curl_slist) hs = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) json_str = g_string_new(NULL);
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = json_generator_new();
	g_autoptr(JsonNode) json_root = NULL;

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
	if (!fu_redfish_backend_create_session(backend, error))
		return FALSE;
	sessionkey = fu_redfish_backend_get_session_key(backend);
	fu_progress_step_done(progress);

	/* create the multipart request */
	request = fu_redfish_backend_request_new(backend);
	curl = fu_redfish_request_get_curl(request);
	mime = curl_mime_init(curl);

	/* create header */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "UpdateRepository");
	json_builder_add_boolean_value(builder, FALSE);
	json_builder_set_member_name(builder, "UpdateTarget");
	json_builder_add_boolean_value(builder, TRUE);
	json_builder_set_member_name(builder, "ETag");
	json_builder_add_string_value(builder, "atag");
	json_builder_end_object(builder);

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	json_generator_to_gstring(json_generator, json_str);

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
fu_redfish_hpe_device_set_progress(FuDevice *self, FuProgress *progress)
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
