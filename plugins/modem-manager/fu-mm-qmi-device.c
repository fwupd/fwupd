/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libqmi-glib.h>

#include "fu-mm-qmi-device.h"

struct _FuMmQmiDevice {
	FuMmDevice parent_instance;
	QmiDevice *qmi_device;
	QmiClientPdc *qmi_client;
	GArray *active_id;
};

G_DEFINE_TYPE(FuMmQmiDevice, fu_mm_qmi_device, FU_TYPE_MM_DEVICE)

#define FU_QMI_PDC_MAX_OPEN_ATTEMPTS 8

typedef struct {
	GMainLoop *mainloop;
	QmiDevice *qmi_device;
	QmiClientPdc *qmi_client;
	GError *error;
	guint open_attempts;
} FuMmQmiDeviceOpenContext;

static void
fu_mm_qmi_device_qmi_device_open_attempt(FuMmQmiDeviceOpenContext *ctx);

static void
fu_mm_qmi_device_qmi_device_open_abort_ready(GObject *qmi_device,
					     GAsyncResult *res,
					     gpointer user_data)
{
	FuMmQmiDeviceOpenContext *ctx = (FuMmQmiDeviceOpenContext *)user_data;

	g_warn_if_fail(ctx->error != NULL);

	/* ignore errors when aborting open */
	qmi_device_close_finish(QMI_DEVICE(qmi_device), res, NULL);

	ctx->open_attempts--;
	if (ctx->open_attempts == 0) {
		g_clear_object(&ctx->qmi_client);
		g_clear_object(&ctx->qmi_device);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	/* retry */
	g_clear_error(&ctx->error);
	fu_mm_qmi_device_qmi_device_open_attempt(ctx);
}

static void
fu_mm_qmi_device_open_abort(FuMmQmiDeviceOpenContext *ctx)
{
	qmi_device_close_async(ctx->qmi_device,
			       15,
			       NULL,
			       fu_mm_qmi_device_qmi_device_open_abort_ready,
			       ctx);
}

static void
fu_mm_qmi_device_qmi_device_allocate_client_ready(GObject *qmi_device,
						  GAsyncResult *res,
						  gpointer user_data)
{
	FuMmQmiDeviceOpenContext *ctx = (FuMmQmiDeviceOpenContext *)user_data;

	ctx->qmi_client = QMI_CLIENT_PDC(
	    qmi_device_allocate_client_finish(QMI_DEVICE(qmi_device), res, &ctx->error));
	if (ctx->qmi_client == NULL) {
		fu_mm_qmi_device_open_abort(ctx);
		return;
	}

	g_main_loop_quit(ctx->mainloop);
}

static void
fu_mm_qmi_device_qmi_device_open_cb(GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	FuMmQmiDeviceOpenContext *ctx = (FuMmQmiDeviceOpenContext *)user_data;

	if (!qmi_device_open_finish(QMI_DEVICE(qmi_device), res, &ctx->error)) {
		fu_mm_qmi_device_open_abort(ctx);
		return;
	}

	qmi_device_allocate_client(ctx->qmi_device,
				   QMI_SERVICE_PDC,
				   QMI_CID_NONE,
				   5,
				   NULL,
				   fu_mm_qmi_device_qmi_device_allocate_client_ready,
				   ctx);
}

static void
fu_mm_qmi_device_qmi_device_open_attempt(FuMmQmiDeviceOpenContext *ctx)
{
	g_debug("trying to open QMI device...");
	qmi_device_open(
	    ctx->qmi_device,
	    QMI_DEVICE_OPEN_FLAGS_AUTO |		   /* detect QMI and MBIM ports */
		QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS | /* pdc requires indications */
		QMI_DEVICE_OPEN_FLAGS_PROXY,		   /* all comms through the proxy */
	    5,
	    NULL,
	    fu_mm_qmi_device_qmi_device_open_cb,
	    ctx);
}

static void
fu_mm_qmi_device_qmi_device_new_ready(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMmQmiDeviceOpenContext *ctx = (FuMmQmiDeviceOpenContext *)user_data;

	ctx->qmi_device = qmi_device_new_finish(res, &ctx->error);
	if (ctx->qmi_device == NULL) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	fu_mm_qmi_device_qmi_device_open_attempt(ctx);
}

static gboolean
fu_mm_qmi_device_open(FuDevice *device, GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	g_autoptr(GFile) qmi_device_file = NULL;
	FuMmQmiDeviceOpenContext ctx = {
	    .mainloop = mainloop,
	    .qmi_device = NULL,
	    .qmi_client = NULL,
	    .error = NULL,
	    .open_attempts = FU_QMI_PDC_MAX_OPEN_ATTEMPTS,
	};

	qmi_device_file = g_file_new_for_path(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)));
	qmi_device_new(qmi_device_file, NULL, fu_mm_qmi_device_qmi_device_new_ready, &ctx);
	g_main_loop_run(mainloop);

	/* either we have all device, client and config list  set, or otherwise error is set */

	if (ctx.qmi_device != NULL && ctx.qmi_client != NULL) {
		g_warn_if_fail(!ctx.error);
		self->qmi_device = ctx.qmi_device;
		self->qmi_client = ctx.qmi_client;
		/* success */
		return TRUE;
	}

	g_propagate_error(error, ctx.error);
	return FALSE;
}

typedef struct {
	GMainLoop *mainloop;
	QmiDevice *qmi_device;
	QmiClientPdc *qmi_client;
	GError *error;
} FuMmQmiDeviceCloseContext;

static void
fu_mm_qmi_device_qmi_device_close_ready(GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	FuMmQmiDeviceCloseContext *ctx = (FuMmQmiDeviceCloseContext *)user_data;

	/* ignore errors when closing if we had one already set when releasing client */
	qmi_device_close_finish(QMI_DEVICE(qmi_device),
				res,
				(ctx->error == NULL) ? &ctx->error : NULL);
	g_clear_object(&ctx->qmi_device);
	g_main_loop_quit(ctx->mainloop);
}

static void
fu_mm_qmi_device_qmi_device_release_client_ready(GObject *qmi_device,
						 GAsyncResult *res,
						 gpointer user_data)
{
	FuMmQmiDeviceCloseContext *ctx = (FuMmQmiDeviceCloseContext *)user_data;

	qmi_device_release_client_finish(QMI_DEVICE(qmi_device), res, &ctx->error);
	g_clear_object(&ctx->qmi_client);

	qmi_device_close_async(ctx->qmi_device,
			       15,
			       NULL,
			       fu_mm_qmi_device_qmi_device_close_ready,
			       ctx);
}

static gboolean
fu_mm_qmi_device_close(FuDevice *device, GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	FuMmQmiDeviceCloseContext ctx = {
	    .mainloop = mainloop,
	    .qmi_device = g_steal_pointer(&self->qmi_device),
	    .qmi_client = g_steal_pointer(&self->qmi_client),
	};

	/* sanity check */
	if (ctx.qmi_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no qmi_device");
		return FALSE;
	}

	qmi_device_release_client(ctx.qmi_device,
				  QMI_CLIENT(ctx.qmi_client),
				  QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
				  5,
				  NULL,
				  fu_mm_qmi_device_qmi_device_release_client_ready,
				  &ctx);
	g_main_loop_run(mainloop);

	/* we should always have both device and client cleared, and optionally error set */

	if (ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return FALSE;
	}

	return TRUE;
}

#define QMI_LOAD_CHUNK_SIZE 0x400

typedef struct {
	GMainLoop *mainloop;
	QmiClientPdc *qmi_client;
	GError *error;
	gulong indication_id;
	guint timeout_id;
	GBytes *blob;
	GArray *digest;
	gsize offset;
	guint token;
} FuMmQmiDeviceWriteContext;

static void
fu_mm_qmi_device_load_config(FuMmQmiDeviceWriteContext *ctx);

static gboolean
fu_mm_qmi_device_load_config_timeout(gpointer user_data)
{
	FuMmQmiDeviceWriteContext *ctx = user_data;

	ctx->timeout_id = 0;
	g_signal_handler_disconnect(ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	g_set_error_literal(&ctx->error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "couldn't load mcfg: timed out");
	g_main_loop_quit(ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static void
fu_mm_qmi_device_load_config_indication(QmiClientPdc *client,
					QmiIndicationPdcLoadConfigOutput *output,
					FuMmQmiDeviceWriteContext *ctx)
{
	gboolean frame_reset;
	guint32 remaining_size;
	guint16 error_code = 0;

	g_source_remove(ctx->timeout_id);
	ctx->timeout_id = 0;
	g_signal_handler_disconnect(ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	if (!qmi_indication_pdc_load_config_output_get_indication_result(output,
									 &error_code,
									 &ctx->error)) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (error_code != 0) {
		/* when a given mcfg file already exists in the device, an "invalid id" error is
		 * returned; the error naming here is a bit off, as the same protocol error number
		 * is used both for 'invalid id' and 'invalid qos id'
		 */
		if (error_code == QMI_PROTOCOL_ERROR_INVALID_QOS_ID) {
			g_debug("file already available in device");
			g_main_loop_quit(ctx->mainloop);
			return;
		}

		g_set_error(&ctx->error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "couldn't load mcfg: %s",
			    qmi_protocol_error_get_string((QmiProtocolError)error_code));
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (qmi_indication_pdc_load_config_output_get_frame_reset(output, &frame_reset, NULL) &&
	    frame_reset) {
		g_set_error(&ctx->error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "couldn't load mcfg: sent data discarded");
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!qmi_indication_pdc_load_config_output_get_remaining_size(output,
								      &remaining_size,
								      &ctx->error)) {
		g_prefix_error_literal(&ctx->error, "couldn't load remaining size: ");
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (remaining_size == 0) {
		g_debug("finished loading mcfg");
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	g_debug("loading next chunk (%u bytes remaining)", remaining_size);
	fu_mm_qmi_device_load_config(ctx);
}

static void
fu_mm_qmi_device_load_config_ready(GObject *qmi_client, GAsyncResult *res, gpointer user_data)
{
	FuMmQmiDeviceWriteContext *ctx = (FuMmQmiDeviceWriteContext *)user_data;
	g_autoptr(QmiMessagePdcLoadConfigOutput) output = NULL;

	output = qmi_client_pdc_load_config_finish(QMI_CLIENT_PDC(qmi_client), res, &ctx->error);
	if (output == NULL) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!qmi_message_pdc_load_config_output_get_result(output, &ctx->error)) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	/* after receiving the response to our request, we now expect an indication
	 * with the actual result of the operation */
	g_warn_if_fail(ctx->indication_id == 0);
	ctx->indication_id = g_signal_connect(QMI_CLIENT_PDC(ctx->qmi_client),
					      "load-config",
					      G_CALLBACK(fu_mm_qmi_device_load_config_indication),
					      ctx);

	/* don't wait forever */
	g_warn_if_fail(ctx->timeout_id == 0);
	ctx->timeout_id = g_timeout_add_seconds(5, fu_mm_qmi_device_load_config_timeout, ctx);
}

static void
fu_mm_qmi_device_load_config(FuMmQmiDeviceWriteContext *ctx)
{
	g_autoptr(QmiMessagePdcLoadConfigInput) input = NULL;
	g_autoptr(GArray) chunk = NULL;
	gsize full_size;
	gsize chunk_size;
	g_autoptr(GError) error = NULL;

	input = qmi_message_pdc_load_config_input_new();
	qmi_message_pdc_load_config_input_set_token(input, ctx->token++, NULL);

	full_size = g_bytes_get_size(ctx->blob);
	if ((ctx->offset + QMI_LOAD_CHUNK_SIZE) > full_size)
		chunk_size = full_size - ctx->offset;
	else
		chunk_size = QMI_LOAD_CHUNK_SIZE;

	chunk = g_array_sized_new(FALSE, FALSE, sizeof(guint8), chunk_size);
	g_array_set_size(chunk, chunk_size);
	if (!fu_memcpy_safe((guint8 *)chunk->data,
			    chunk_size,
			    0x0,					       /* dst */
			    (const guint8 *)g_bytes_get_data(ctx->blob, NULL), /* src */
			    g_bytes_get_size(ctx->blob),
			    ctx->offset,
			    chunk_size,
			    &error)) {
		g_critical("failed to copy chunk: %s", error->message);
	}

	qmi_message_pdc_load_config_input_set_config_chunk(input,
							   QMI_PDC_CONFIGURATION_TYPE_SOFTWARE,
							   ctx->digest,
							   full_size,
							   chunk,
							   NULL);
	ctx->offset += chunk_size;

	qmi_client_pdc_load_config(ctx->qmi_client,
				   input,
				   10,
				   NULL,
				   fu_mm_qmi_device_load_config_ready,
				   ctx);
}

static GArray *
fu_mm_qmi_device_get_checksum(GBytes *blob)
{
	gsize file_size;
	gsize hash_size;
	GArray *digest;
	g_autoptr(GChecksum) checksum = NULL;

	/* get checksum, to be used as unique id */
	file_size = g_bytes_get_size(blob);
	hash_size = g_checksum_type_get_length(G_CHECKSUM_SHA1);
	checksum = g_checksum_new(G_CHECKSUM_SHA1);
	g_checksum_update(checksum, g_bytes_get_data(blob, NULL), file_size);
	/* libqmi expects a GArray of bytes, not a GByteArray */
	digest = g_array_sized_new(FALSE, FALSE, sizeof(guint8), hash_size);
	g_array_set_size(digest, hash_size);
	g_checksum_get_digest(checksum, (guint8 *)digest->data, &hash_size);

	return digest;
}

static GArray *
fu_mm_qmi_device_write(FuMmQmiDevice *self, const gchar *filename, GBytes *blob, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	g_autoptr(GArray) digest = fu_mm_qmi_device_get_checksum(blob);
	FuMmQmiDeviceWriteContext ctx = {
	    .mainloop = mainloop,
	    .qmi_client = self->qmi_client,
	    .error = NULL,
	    .indication_id = 0,
	    .timeout_id = 0,
	    .blob = blob,
	    .digest = digest,
	    .offset = 0,
	    .token = 0,
	};

	fu_mm_qmi_device_load_config(&ctx);
	g_main_loop_run(mainloop);

	if (ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return NULL;
	}

	return g_steal_pointer(&digest);
}

typedef struct {
	GMainLoop *mainloop;
	QmiClientPdc *qmi_client;
	GError *error;
	gulong indication_id;
	guint timeout_id;
	GArray *digest;
	guint token;
} FuMmQmiDeviceActivateContext;

static gboolean
fu_mm_qmi_device_activate_config_timeout(gpointer user_data)
{
	FuMmQmiDeviceActivateContext *ctx = user_data;

	ctx->timeout_id = 0;
	g_signal_handler_disconnect(ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	/* not an error, the device may go away without sending the indication */
	g_main_loop_quit(ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static void
fu_mm_qmi_device_activate_config_indication(QmiClientPdc *client,
					    QmiIndicationPdcActivateConfigOutput *output,
					    FuMmQmiDeviceActivateContext *ctx)
{
	guint16 error_code = 0;

	g_source_remove(ctx->timeout_id);
	ctx->timeout_id = 0;
	g_signal_handler_disconnect(ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	if (!qmi_indication_pdc_activate_config_output_get_indication_result(output,
									     &error_code,
									     &ctx->error)) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (error_code != 0) {
		g_set_error(&ctx->error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "couldn't activate config: %s",
			    qmi_protocol_error_get_string((QmiProtocolError)error_code));
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	/* assume ok */
	g_debug("successful activate configuration indication: assuming device reset is ongoing");
	g_main_loop_quit(ctx->mainloop);
}

static void
fu_mm_qmi_device_activate_config_ready(GObject *qmi_client, GAsyncResult *res, gpointer user_data)
{
	FuMmQmiDeviceActivateContext *ctx = (FuMmQmiDeviceActivateContext *)user_data;
	g_autoptr(QmiMessagePdcActivateConfigOutput) output = NULL;

	output =
	    qmi_client_pdc_activate_config_finish(QMI_CLIENT_PDC(qmi_client), res, &ctx->error);
	if (output == NULL) {
		/* If we didn't receive a response, this is a good indication that the device
		 * reset itself, we can consider this a successful operation.
		 * Note: not using g_error_matches() to avoid matching the domain, because the
		 * error may be either QMI_CORE_ERROR_TIMEOUT or MBIM_CORE_ERROR_TIMEOUT (same
		 * numeric value), and we don't want to build-depend on libmbim just for this.
		 */
		if (ctx->error->code == QMI_CORE_ERROR_TIMEOUT) {
			g_debug("request to activate configuration timed out: assuming device "
				"reset is ongoing");
			g_clear_error(&ctx->error);
		}
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!qmi_message_pdc_activate_config_output_get_result(output, &ctx->error)) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	/* When we activate the config, if the operation is successful, we'll just
	 * see the modem going away completely. So, do not consider an error the timeout
	 * waiting for the Activate Config indication, as that is actually a good
	 * thing.
	 */
	g_warn_if_fail(ctx->indication_id == 0);
	ctx->indication_id =
	    g_signal_connect(QMI_CLIENT_PDC(ctx->qmi_client),
			     "activate-config",
			     G_CALLBACK(fu_mm_qmi_device_activate_config_indication),
			     ctx);

	/* don't wait forever */
	g_warn_if_fail(ctx->timeout_id == 0);
	ctx->timeout_id = g_timeout_add_seconds(5, fu_mm_qmi_device_activate_config_timeout, ctx);
}

static void
fu_mm_qmi_device_activate_config(FuMmQmiDeviceActivateContext *ctx)
{
	g_autoptr(QmiMessagePdcActivateConfigInput) input = NULL;

	input = qmi_message_pdc_activate_config_input_new();
	qmi_message_pdc_activate_config_input_set_config_type(input,
							      QMI_PDC_CONFIGURATION_TYPE_SOFTWARE,
							      NULL);
	qmi_message_pdc_activate_config_input_set_token(input, ctx->token++, NULL);

	g_debug("activating selected configuration...");
	qmi_client_pdc_activate_config(ctx->qmi_client,
				       input,
				       5,
				       NULL,
				       fu_mm_qmi_device_activate_config_ready,
				       ctx);
}

static gboolean
fu_mm_qmi_device_set_selected_config_timeout(gpointer user_data)
{
	FuMmQmiDeviceActivateContext *ctx = user_data;

	ctx->timeout_id = 0;
	g_signal_handler_disconnect(ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	g_set_error_literal(&ctx->error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "couldn't set selected config: timed out");
	g_main_loop_quit(ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static void
fu_mm_qmi_device_set_selected_config_indication(QmiClientPdc *client,
						QmiIndicationPdcSetSelectedConfigOutput *output,
						FuMmQmiDeviceActivateContext *ctx)
{
	guint16 error_code = 0;

	g_source_remove(ctx->timeout_id);
	ctx->timeout_id = 0;
	g_signal_handler_disconnect(ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	if (!qmi_indication_pdc_set_selected_config_output_get_indication_result(output,
										 &error_code,
										 &ctx->error)) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (error_code != 0) {
		g_set_error(&ctx->error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "couldn't set selected config: %s",
			    qmi_protocol_error_get_string((QmiProtocolError)error_code));
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	g_debug("current configuration successfully selected...");

	/* now activate config */
	fu_mm_qmi_device_activate_config(ctx);
}

static void
fu_mm_qmi_device_set_selected_config_ready(GObject *qmi_client,
					   GAsyncResult *res,
					   gpointer user_data)
{
	FuMmQmiDeviceActivateContext *ctx = (FuMmQmiDeviceActivateContext *)user_data;
	g_autoptr(QmiMessagePdcSetSelectedConfigOutput) output = NULL;

	output =
	    qmi_client_pdc_set_selected_config_finish(QMI_CLIENT_PDC(qmi_client), res, &ctx->error);
	if (output == NULL) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!qmi_message_pdc_set_selected_config_output_get_result(output, &ctx->error)) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	/* after receiving the response to our request, we now expect an indication
	 * with the actual result of the operation */
	g_warn_if_fail(ctx->indication_id == 0);
	ctx->indication_id =
	    g_signal_connect(QMI_CLIENT_PDC(ctx->qmi_client),
			     "set-selected-config",
			     G_CALLBACK(fu_mm_qmi_device_set_selected_config_indication),
			     ctx);

	/* don't wait forever */
	g_warn_if_fail(ctx->timeout_id == 0);
	ctx->timeout_id =
	    g_timeout_add_seconds(5, fu_mm_qmi_device_set_selected_config_timeout, ctx);
}

static void
fu_mm_qmi_device_set_selected_config(FuMmQmiDeviceActivateContext *ctx)
{
	g_autoptr(QmiMessagePdcSetSelectedConfigInput) input = NULL;

	input = qmi_message_pdc_set_selected_config_input_new();
	qmi_message_pdc_set_selected_config_input_set_type_with_id_v2(
	    input,
	    QMI_PDC_CONFIGURATION_TYPE_SOFTWARE,
	    ctx->digest,
	    NULL);
	qmi_message_pdc_set_selected_config_input_set_token(input, ctx->token++, NULL);

	g_debug("selecting current configuration...");
	qmi_client_pdc_set_selected_config(ctx->qmi_client,
					   input,
					   10,
					   NULL,
					   fu_mm_qmi_device_set_selected_config_ready,
					   ctx);
}

static gboolean
fu_mm_qmi_device_activate(FuMmQmiDevice *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	FuMmQmiDeviceActivateContext ctx = {
	    .mainloop = mainloop,
	    .qmi_client = self->qmi_client,
	    .error = NULL,
	    .indication_id = 0,
	    .timeout_id = 0,
	    .digest = self->active_id,
	    .token = 0,
	};

	fu_mm_qmi_device_set_selected_config(&ctx);
	g_main_loop_run(mainloop);

	if (ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_qmi_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);

	/* ignore action if there is no active id specified */
	if (self->active_id == NULL)
		return TRUE;
	return fu_mm_qmi_device_activate(self, error);
}

static gboolean
fu_mm_qmi_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	//	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	return TRUE;
}

static gboolean
fu_mm_qmi_device_probe(FuDevice *device, GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_QMI, error);
}

typedef struct {
	gchar *filename;
	GBytes *bytes;
	GArray *digest;
	gboolean active;
} FuMmQmiDeviceFileInfo;

static void
fu_mm_qmi_device_file_info_free(FuMmQmiDeviceFileInfo *file_info)
{
	g_clear_pointer(&file_info->digest, g_array_unref);
	g_free(file_info->filename);
	g_bytes_unref(file_info->bytes);
	g_free(file_info);
}

typedef struct {
	FuMmQmiDevice *self;
	GError *error;
	GPtrArray *file_infos;
} FuMmQmiDeviceArchiveIterateCtx;

static gboolean
fu_mm_qmi_device_should_be_active(const gchar *version, const gchar *filename)
{
	g_auto(GStrv) split = NULL;
	g_autofree gchar *carrier_id = NULL;

	/* The filename of the mcfg file is composed of a "mcfg." prefix, then the
	 * carrier code, followed by the carrier version, and finally a ".mbn"
	 * prefix. Here we try to guess, based on the carrier code, whether the
	 * specific mcfg file should be activated after the firmware upgrade
	 * operation.
	 *
	 * This logic requires that the previous device version includes the carrier
	 * code also embedded in the version string. E.g. "xxxx.VF.xxxx". If we find
	 * this match, we assume this is the active config to use.
	 */

	split = g_strsplit(filename, ".", -1);
	if (g_strv_length(split) < 4)
		return FALSE;
	if (g_strcmp0(split[0], "mcfg") != 0)
		return FALSE;

	carrier_id = g_strdup_printf(".%s.", split[1]);
	return (g_strstr_len(version, -1, carrier_id) != NULL);
}

static gboolean
fu_mm_qmi_device_archive_iterate_mcfg_cb(FuArchive *archive,
					 const gchar *filename,
					 GBytes *bytes,
					 gpointer user_data,
					 GError **error)
{
	FuMmQmiDeviceArchiveIterateCtx *ctx = user_data;
	FuMmQmiDeviceFileInfo *file_info;

	/* filenames should be named as 'mcfg.*.mbn', e.g.: mcfg.A2.018.mbn */
	if (!g_str_has_prefix(filename, "mcfg.") || !g_str_has_suffix(filename, ".mbn"))
		return TRUE;

	file_info = g_new0(FuMmQmiDeviceFileInfo, 1);
	file_info->filename = g_strdup(filename);
	file_info->bytes = g_bytes_ref(bytes);
	file_info->active =
	    fu_mm_qmi_device_should_be_active(fu_device_get_version(FU_DEVICE(ctx->self)),
					      filename);
	g_ptr_array_add(ctx->file_infos, file_info);
	return TRUE;
}

static gboolean
fu_mm_qmi_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) file_infos =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_mm_qmi_device_file_info_free);
	gint active_i = -1;
	FuMmQmiDeviceArchiveIterateCtx archive_context = {
	    .self = self,
	    .error = NULL,
	    .file_infos = file_infos,
	};

	/* decompress entire archive ahead of time */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* process the list of MCFG files to write */
	if (!fu_archive_iterate(archive,
				fu_mm_qmi_device_archive_iterate_mcfg_cb,
				&archive_context,
				error))
		return FALSE;

	for (guint i = 0; i < file_infos->len; i++) {
		FuMmQmiDeviceFileInfo *file_info = g_ptr_array_index(file_infos, i);
		file_info->digest = fu_mm_qmi_device_write(self,
							   file_info->filename,
							   file_info->bytes,
							   &archive_context.error);
		if (file_info->digest == NULL) {
			g_prefix_error(&archive_context.error,
				       "Failed to write file '%s':",
				       file_info->filename);
			break;
		}
		/* if we wrongly detect more than one, just assume the latest one; this
		 * is not critical, it may just take a bit more time to perform the
		 * automatic carrier config switching in ModemManager */
		if (file_info->active)
			active_i = i;
	}

	/* set expected active configuration */
	if (active_i >= 0 && self->active_id != NULL) {
		FuMmQmiDeviceFileInfo *file_info = g_ptr_array_index(file_infos, active_i);
		self->active_id = g_array_ref(file_info->digest);
	}

	if (archive_context.error != NULL) {
		g_propagate_error(error, archive_context.error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qmi_device_prepare(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_qmi_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_qmi_device_set_progress(FuDevice *self, FuProgress *progress)
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
fu_mm_qmi_device_init(FuMmQmiDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.qmi_pdc");
}

static void
fu_mm_qmi_device_finalize(GObject *object)
{
	FuMmQmiDevice *self = FU_MM_QMI_DEVICE(object);

	if (self->active_id)
		g_array_unref(self->active_id);

	G_OBJECT_CLASS(fu_mm_qmi_device_parent_class)->finalize(object);
}

static void
fu_mm_qmi_device_class_init(FuMmQmiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_qmi_device_finalize;
	device_class->attach = fu_mm_qmi_device_attach;
	device_class->detach = fu_mm_qmi_device_detach;
	device_class->open = fu_mm_qmi_device_open;
	device_class->close = fu_mm_qmi_device_close;
	device_class->prepare = fu_mm_qmi_device_prepare;
	device_class->cleanup = fu_mm_qmi_device_cleanup;
	device_class->probe = fu_mm_qmi_device_probe;
	device_class->set_progress = fu_mm_qmi_device_set_progress;
	device_class->write_firmware = fu_mm_qmi_device_write_firmware;
}
