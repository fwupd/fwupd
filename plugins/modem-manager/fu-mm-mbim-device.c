/*
 * Copyright 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libmbim-glib.h>

#include "fu-mm-mbim-device.h"

struct _FuMmMbimDevice {
	FuMmDevice parent_instance;
	MbimDevice *mbim_device;
};

G_DEFINE_TYPE(FuMmMbimDevice, fu_mm_mbim_device, FU_TYPE_MM_DEVICE)

#define FU_MM_MBIM_DEVICE_MAX_OPEN_ATTEMPTS 8

#define FU_MM_MBIM_DEVICE_TIMEOUT_MS 1500

static gboolean
fu_mm_mbim_device_error_convert(GError **error)
{
	const FuErrorConvertEntry entries[] = {
	    /* clang-format off */
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_FAILED, FWUPD_ERROR_INTERNAL},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_WRONG_STATE, FWUPD_ERROR_INTERNAL},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_TIMEOUT, FWUPD_ERROR_TIMED_OUT},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_INVALID_ARGS, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_INVALID_MESSAGE, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_UNSUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_ABORTED, FWUPD_ERROR_INTERNAL},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_UNKNOWN_STATE, FWUPD_ERROR_INTERNAL},
	    {MBIM_CORE_ERROR, MBIM_CORE_ERROR_INCOMPLETE_MESSAGE, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_INVALID, FWUPD_ERROR_INTERNAL},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_TIMEOUT_FRAGMENT, FWUPD_ERROR_TIMED_OUT},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_FRAGMENT_OUT_OF_SEQUENCE, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_LENGTH_MISMATCH, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_DUPLICATED_TID, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_NOT_OPENED, FWUPD_ERROR_INTERNAL},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_UNKNOWN, FWUPD_ERROR_INTERNAL},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_CANCEL, FWUPD_ERROR_INTERNAL},
	    {MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_MAX_TRANSFER, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NONE, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_BUSY, FWUPD_ERROR_BUSY},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FAILURE, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SIM_NOT_INSERTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_BAD_SIM, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED, FWUPD_ERROR_AUTH_EXPIRED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_DISABLED, FWUPD_ERROR_AUTH_FAILED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NOT_REGISTERED, FWUPD_ERROR_AUTH_FAILED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PROVIDERS_NOT_FOUND, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NO_DEVICE_SUPPORT, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PROVIDER_NOT_VISIBLE, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_DATA_CLASS_NOT_AVAILABLE, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PACKET_SERVICE_DETACHED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_MAX_ACTIVATED_CONTEXTS, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NOT_INITIALIZED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_VOICE_CALL_IN_PROGRESS, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_CONTEXT_NOT_ACTIVATED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SERVICE_NOT_ACTIVATED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_ACCESS_STRING, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_USER_NAME_PWD, FWUPD_ERROR_AUTH_FAILED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_RADIO_POWER_OFF, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_PARAMETERS, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_READ_FAILURE, FWUPD_ERROR_READ},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_WRITE_FAILURE, FWUPD_ERROR_WRITE},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NO_PHONEBOOK, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PARAMETER_TOO_LONG, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_STK_BUSY, FWUPD_ERROR_BUSY},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_OPERATION_NOT_ALLOWED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_MEMORY_FAILURE, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_MEMORY_INDEX, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_MEMORY_FULL, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FILTER_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_DSS_INSTANCE_LIMIT, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_DEVICE_SERVICE_OPERATION, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_AUTH_INCORRECT_AUTN, FWUPD_ERROR_AUTH_FAILED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_AUTH_SYNC_FAILURE, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_AUTH_AMF_NOT_SET, FWUPD_ERROR_INTERNAL},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_CONTEXT_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SMS_UNKNOWN_SMSC_ADDRESS, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SMS_NETWORK_TIMEOUT, FWUPD_ERROR_TIMED_OUT},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SMS_LANG_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SMS_ENCODING_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SMS_FORMAT_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
#if MBIM_CHECK_VERSION(1, 30, 0)
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_SIGNATURE, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_IMEI, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_INVALID_TIMESTAMP, FWUPD_ERROR_INVALID_DATA},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NETWORK_LIST_TOO_LARGE, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SIGNATURE_ALGORITHM_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FEATURE_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_DECODE_OR_PARSING_ERROR, FWUPD_ERROR_INVALID_DATA},
#endif
	    /* clang-format on */
	};
	return fu_error_convert(entries, G_N_ELEMENTS(entries), error);
}

typedef struct {
	gboolean ret;
	GMainContext *main_ctx;
	GMainLoop *loop;
	GCancellable *cancellable;
	guint timeout_id;
	MbimDevice *mbim_device;
	MbimMessage *mbim_message;
	GError *error;
} FuMmMbimDeviceHelper;

static gboolean
fu_mm_mbim_device_helper_timeout_cb(gpointer user_data)
{
	FuMmMbimDeviceHelper *helper = (FuMmMbimDeviceHelper *)user_data;
	g_cancellable_cancel(helper->cancellable);
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static FuMmMbimDeviceHelper *
fu_mm_mbim_device_helper_helper_new(guint timeout_ms)
{
	FuMmMbimDeviceHelper *helper = g_new0(FuMmMbimDeviceHelper, 1);
	g_autoptr(GSource) source = g_timeout_source_new(timeout_ms);
	helper->cancellable = g_cancellable_new();
	helper->main_ctx = g_main_context_new();
	helper->loop = g_main_loop_new(helper->main_ctx, FALSE);
	g_source_set_callback(source, fu_mm_mbim_device_helper_timeout_cb, helper, NULL);
	g_source_attach(source, helper->main_ctx);
	g_main_context_push_thread_default(helper->main_ctx);
	return helper;
}

static void
fu_mm_mbim_device_helper_free(FuMmMbimDeviceHelper *helper)
{
	g_main_context_pop_thread_default(helper->main_ctx);
	if (helper->timeout_id != 0)
		g_source_remove(helper->timeout_id);
	if (helper->mbim_device != NULL)
		g_object_unref(helper->mbim_device);
	if (helper->mbim_message != NULL)
		mbim_message_unref(helper->mbim_message);
	g_object_unref(helper->cancellable);
	g_main_loop_unref(helper->loop);
	g_main_context_unref(helper->main_ctx);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMmMbimDeviceHelper, fu_mm_mbim_device_helper_free)

static void
fu_mm_mbim_device_new_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceHelper *helper = (FuMmMbimDeviceHelper *)user_data;
	helper->mbim_device = mbim_device_new_finish(res, &helper->error);
	g_main_loop_quit(helper->loop);
}

static MbimDevice *
fu_mm_mbim_device_new_sync(FuMmMbimDevice *self, GFile *file, guint timeout_ms, GError **error)
{
	g_autoptr(FuMmMbimDeviceHelper) helper = fu_mm_mbim_device_helper_helper_new(timeout_ms);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(G_IS_FILE(file), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *path = g_file_get_path(file);
		event_id = g_strdup_printf("MbimDeviceNew:Path=%s", path);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_object_new(MBIM_TYPE_DEVICE, "device-file", file, NULL);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	mbim_device_new(file, helper->cancellable, fu_mm_mbim_device_new_cb, helper);
	g_main_loop_run(helper->loop);

	/* save response */
	if (helper->mbim_device == NULL) {
		fu_mm_mbim_device_error_convert(&helper->error);
		if (event != NULL)
			fu_device_event_set_error(event, helper->error);
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}

	/* success */
	return g_steal_pointer(&helper->mbim_device);
}

static void
fu_mm_mbim_device_open_sync_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceHelper *helper = (FuMmMbimDeviceHelper *)user_data;
	helper->ret = mbim_device_open_full_finish(helper->mbim_device, res, &helper->error);
	g_main_loop_quit(helper->loop);
}

static gboolean
fu_mm_mbim_device_open_sync(FuMmMbimDevice *self, guint timeout_ms, GError **error)
{
	g_autoptr(FuMmMbimDeviceHelper) helper = fu_mm_mbim_device_helper_helper_new(timeout_ms);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup("MbimDeviceOpen");
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		return event != NULL;
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	mbim_device_open_full(self->mbim_device,
			      MBIM_DEVICE_OPEN_FLAGS_PROXY,
			      10,
			      helper->cancellable,
			      fu_mm_mbim_device_open_sync_cb,
			      helper);
	g_main_loop_run(helper->loop);

	/* save response */
	if (!helper->ret) {
		fu_mm_mbim_device_error_convert(&helper->error);
		if (event != NULL)
			fu_device_event_set_error(event, helper->error);
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_mm_mbim_device_close_sync_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceHelper *helper = (FuMmMbimDeviceHelper *)user_data;
	helper->ret = mbim_device_close_finish(helper->mbim_device, res, &helper->error);
	g_main_loop_quit(helper->loop);
}

static gboolean
fu_mm_mbim_device_close_sync(FuMmMbimDevice *self, guint timeout_ms, GError **error)
{
	g_autoptr(FuMmMbimDeviceHelper) helper = fu_mm_mbim_device_helper_helper_new(timeout_ms);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup("MbimDeviceClose");
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		g_clear_object(&self->mbim_device);
		return event != NULL;
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	mbim_device_close(self->mbim_device,
			  5,
			  helper->cancellable,
			  fu_mm_mbim_device_close_sync_cb,
			  helper);
	g_main_loop_run(helper->loop);

	/* save response */
	if (!helper->ret) {
		fu_mm_mbim_device_error_convert(&helper->error);
		if (event != NULL)
			fu_device_event_set_error(event, helper->error);
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}

	/* success */
	g_clear_object(&self->mbim_device);
	return TRUE;
}

static void
fu_mm_mbim_device_command_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceHelper *helper = (FuMmMbimDeviceHelper *)user_data;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(helper->mbim_device, res, &helper->error);
	if (response != NULL) {
		if (mbim_message_response_get_result(response,
						     MBIM_MESSAGE_TYPE_COMMAND_DONE,
						     &helper->error)) {
			helper->mbim_message = g_steal_pointer(&response);
		}
	}
	g_main_loop_quit(helper->loop);
}

static MbimMessage *
fu_mm_mbim_device_command_sync(FuMmMbimDevice *self,
			       MbimMessage *mbim_message,
			       guint timeout_ms,
			       GError **error)
{
	g_autoptr(FuMmMbimDeviceHelper) helper = fu_mm_mbim_device_helper_helper_new(timeout_ms);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		const guint8 *buf;
		guint32 bufsz = 0;
		g_autofree gchar *data_base64 = NULL;

		buf = mbim_message_get_raw(mbim_message, &bufsz, error);
		if (buf == NULL) {
			fu_mm_mbim_device_error_convert(error);
			return NULL;
		}
		data_base64 = g_base64_encode(buf, bufsz);
		event_id = g_strdup_printf("MbimDeviceCommand:Data=%s,Length=0x%x",
					   data_base64,
					   (guint)bufsz);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		g_autoptr(GBytes) blob = NULL;
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		blob = fu_device_event_get_bytes(event, "Data", error);
		if (blob == NULL)
			return NULL;
		return mbim_message_new(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	mbim_device_command(self->mbim_device,
			    mbim_message,
			    2 * timeout_ms / 1000,
			    helper->cancellable,
			    fu_mm_mbim_device_command_cb,
			    helper);
	g_main_loop_run(helper->loop);

	/* save response */
	if (helper->mbim_message == NULL) {
		fu_mm_mbim_device_error_convert(&helper->error);
		if (event != NULL)
			fu_device_event_set_error(event, helper->error);
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}

	/* save response */
	if (event != NULL) {
		const guint8 *buf;
		guint32 bufsz = 0;

		buf = mbim_message_get_raw(helper->mbim_message, &bufsz, error);
		if (buf == NULL) {
			fu_mm_mbim_device_error_convert(error);
			return NULL;
		}
		fu_device_event_set_data(event, "Data", buf, bufsz);
	}

	/* success */
	return g_steal_pointer(&helper->mbim_message);
}

static gboolean
fu_mm_mbim_device_ensure_firmware_version(FuMmMbimDevice *self, GError **error)
{
	g_autofree gchar *firmware_version = NULL;
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	request = mbim_message_device_caps_query_new(NULL);
	response = fu_mm_mbim_device_command_sync(self, request, 10 * 1000, error);
	if (response == NULL)
		return FALSE;
	if (!mbim_message_device_caps_response_parse(response,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     NULL,
						     &firmware_version,
						     NULL,
						     error)) {
		fu_mm_mbim_device_error_convert(error);
		g_prefix_error(error, "failed to parse caps-query response: ");
		return FALSE;
	}
	g_debug("[%s] modem query caps firmware version: %s",
		mbim_device_get_path_display(self->mbim_device),
		firmware_version);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mbim_device_detach_to_edl(FuMmMbimDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	request = mbim_message_qdu_quectel_reboot_set_new(MBIM_QDU_QUECTEL_REBOOT_TYPE_EDL, NULL);
	response = fu_mm_mbim_device_command_sync(self, request, 5 * 1000, &error_local);
	if (response == NULL) {
		/* MBIM port goes away */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mbim_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);

	/* use special command for Quectel MBIM devices */
	if (fu_device_get_vid(device) == 0x2C7C) {
		if (!fu_mm_mbim_device_detach_to_edl(self, error))
			return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static GArray *
fu_mm_mbim_device_get_checksum(GBytes *blob)
{
	gsize file_size;
	gsize hash_size;
	GArray *digest;
	g_autoptr(GChecksum) checksum = NULL;

	/* get checksum, to be used as unique id */
	file_size = g_bytes_get_size(blob);
	hash_size = g_checksum_type_get_length(G_CHECKSUM_SHA256);
	checksum = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(checksum, g_bytes_get_data(blob, NULL), file_size);

	/* libqmi expects a GArray of bytes, not a GByteArray */
	digest = g_array_sized_new(FALSE, FALSE, sizeof(guint8), hash_size);
	g_array_set_size(digest, hash_size);
	g_checksum_get_digest(checksum, (guint8 *)digest->data, &hash_size);
	return digest;
}

static gboolean
fu_mm_mbim_device_write_chunk(FuMmMbimDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	request = mbim_message_qdu_file_write_set_new(fu_chunk_get_data_sz(chk),
						      fu_chunk_get_data(chk),
						      NULL);
	response = fu_mm_mbim_device_command_sync(self, request, 20 * 1000, error);
	if (response == NULL)
		return FALSE;
	if (!mbim_message_qdu_file_write_response_parse(response, error)) {
		fu_mm_mbim_device_error_convert(error);
		g_prefix_error(error, "failed to parse write-chunk response: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mbim_device_write_chunks(FuMmMbimDevice *self,
			       FuChunkArray *chunks,
			       FuProgress *progress,
			       GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, 0, error);
		if (chk == NULL) {
			g_prefix_error(error, "failed to get chunk: ");
			return FALSE;
		}
		if (!fu_mm_mbim_device_write_chunk(self, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mbim_device_write(FuMmMbimDevice *self,
			const gchar *filename,
			GBytes *blob,
			FuProgress *progress,
			GError **error)
{
	guint32 out_max_transfer_size = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GArray) checksum = fu_mm_mbim_device_get_checksum(blob);
	g_autoptr(MbimMessage) action_start_req = NULL;
	g_autoptr(MbimMessage) action_start_res = NULL;
	g_autoptr(MbimMessage) file_open_req = NULL;
	g_autoptr(MbimMessage) file_open_res = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "start-update");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "file-open");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "send-chunks");

	/* start update */
	action_start_req = mbim_message_qdu_update_session_set_new(MBIM_QDU_SESSION_ACTION_START,
								   MBIM_QDU_SESSION_TYPE_LE,
								   NULL);
	action_start_res = fu_mm_mbim_device_command_sync(self, action_start_req, 10 * 1000, error);
	if (action_start_res == NULL)
		return FALSE;
	if (!mbim_message_qdu_update_session_response_parse(action_start_res,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    error)) {
		fu_mm_mbim_device_error_convert(error);
		g_prefix_error(error, "failed to parse action-start response: ");
		return FALSE;
	}
	g_debug("[%s] successfully request modem to update session",
		mbim_device_get_path_display(self->mbim_device));
	fu_progress_step_done(progress);

	/* get the max transfer size */
	file_open_req = mbim_message_qdu_file_open_set_new(MBIM_QDU_FILE_TYPE_LITTLE_ENDIAN_PACKAGE,
							   g_bytes_get_size(blob),
							   NULL);
	file_open_res = fu_mm_mbim_device_command_sync(self, file_open_req, 10 * 1000, error);
	if (file_open_res == NULL)
		return FALSE;

	if (!mbim_message_qdu_file_open_response_parse(file_open_res,
						       &out_max_transfer_size,
						       NULL,
						       error)) {
		fu_mm_mbim_device_error_convert(error);
		g_prefix_error(error, "failed to parse file-open response: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send chunks */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       out_max_transfer_size);
	if (!fu_mm_mbim_device_write_chunks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mbim_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);
	XbNode *part = NULL;
	const gchar *filename = NULL;
	const gchar *csum;
	g_autofree gchar *csum_actual = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data_part = NULL;
	g_autoptr(GBytes) data_xml = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* decompress entire archive ahead of time */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* load the manifest of operations */
	data_xml = fu_archive_lookup_by_fn(archive, "flashfile.xml", error);
	if (data_xml == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source, data_xml, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	part = xb_silo_query_first(silo, "parts/part", error);
	if (part == NULL)
		return FALSE;
	filename = xb_node_get_attr(part, "filename");
	csum = xb_node_get_attr(part, "MD5");
	data_part = fu_archive_lookup_by_fn(archive, filename, error);
	if (data_part == NULL)
		return FALSE;
	csum_actual = g_compute_checksum_for_bytes(G_CHECKSUM_MD5, data_part);
	if (g_strcmp0(csum, csum_actual) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "[%s] MD5 not matched",
			    filename);
		return FALSE;
	}
	g_debug("[%s] MD5 matched", filename);

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_mm_mbim_device_write(self, filename, data_part, progress, error))
		return FALSE;

	/* read back new version */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	if (!fu_mm_mbim_device_ensure_firmware_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mbim_device_probe(FuDevice *device, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_MBIM, error);
}

static gboolean
fu_mm_mbim_device_open_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);
	return fu_mm_mbim_device_open_sync(self, FU_MM_MBIM_DEVICE_TIMEOUT_MS, error);
}

static gboolean
fu_mm_mbim_device_open(FuDevice *device, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);
	g_autoptr(GFile) mbim_device_file =
	    g_file_new_for_path(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)));

	/* create and open */
	self->mbim_device =
	    fu_mm_mbim_device_new_sync(self, mbim_device_file, FU_MM_MBIM_DEVICE_TIMEOUT_MS, error);
	if (self->mbim_device == NULL)
		return FALSE;
	return fu_device_retry(device,
			       fu_mm_mbim_device_open_cb,
			       FU_MM_MBIM_DEVICE_MAX_OPEN_ATTEMPTS,
			       NULL,
			       error);
}

static gboolean
fu_mm_mbim_device_close(FuDevice *device, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);

	/* sanity check */
	if (self->mbim_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no mbim_device");
		return FALSE;
	}
	return fu_mm_mbim_device_close_sync(self, FU_MM_MBIM_DEVICE_TIMEOUT_MS, error);
}

static gboolean
fu_mm_mbim_device_prepare(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);

	/* autosuspend delay updated for a proper firmware update */
	if (!fu_mm_device_set_autosuspend_delay(FU_MM_DEVICE(self), 20000, error))
		return FALSE;

	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_mbim_device_cleanup(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);

	if (!fu_mm_device_set_autosuspend_delay(FU_MM_DEVICE(self), 2000, error))
		return FALSE;

	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_mbim_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mm_mbim_device_init(FuMmMbimDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATION_TAG);
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.mbim_qdu");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_mm_mbim_device_finalize(GObject *object)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(object);
	g_warn_if_fail(self->mbim_device == NULL);
	G_OBJECT_CLASS(fu_mm_mbim_device_parent_class)->finalize(object);
}

static void
fu_mm_mbim_device_class_init(FuMmMbimDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_mbim_device_finalize;
	device_class->open = fu_mm_mbim_device_open;
	device_class->close = fu_mm_mbim_device_close;
	device_class->probe = fu_mm_mbim_device_probe;
	device_class->detach = fu_mm_mbim_device_detach;
	device_class->prepare = fu_mm_mbim_device_prepare;
	device_class->cleanup = fu_mm_mbim_device_cleanup;
	device_class->set_progress = fu_mm_mbim_device_set_progress;
	device_class->write_firmware = fu_mm_mbim_device_write_firmware;
}
