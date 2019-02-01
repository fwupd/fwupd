/*
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-qmi-pdc-updater.h"

#define FU_QMI_PDC_MAX_OPEN_ATTEMPTS 8

struct _FuQmiPdcUpdater {
	GObject		parent_instance;
	gchar		*qmi_port;
	QmiDevice	*qmi_device;
	QmiClientPdc	*qmi_client;
};

G_DEFINE_TYPE (FuQmiPdcUpdater, fu_qmi_pdc_updater, G_TYPE_OBJECT)

typedef struct {
	GMainLoop	*mainloop;
	QmiDevice	*qmi_device;
	QmiClientPdc	*qmi_client;
	GError		*error;
	guint		open_attempts;
} OpenContext;

static void fu_qmi_pdc_updater_qmi_device_open_attempt (OpenContext *ctx);

static void
fu_qmi_pdc_updater_qmi_device_open_abort_ready (GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	g_assert (ctx->error != NULL);

	/* ignore errors when aborting open */
	qmi_device_close_finish (QMI_DEVICE (qmi_device), res, NULL);

	ctx->open_attempts--;
	if (ctx->open_attempts == 0) {
		g_clear_object (&ctx->qmi_client);
		g_clear_object (&ctx->qmi_device);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	/* retry */
	g_clear_error (&ctx->error);
	fu_qmi_pdc_updater_qmi_device_open_attempt (ctx);
}

static void
fu_qmi_pdc_updater_open_abort (OpenContext *ctx)
{
	qmi_device_close_async (ctx->qmi_device,
				15, NULL, fu_qmi_pdc_updater_qmi_device_open_abort_ready, ctx);
}

static void
fu_qmi_pdc_updater_qmi_device_allocate_client_ready (GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	ctx->qmi_client = QMI_CLIENT_PDC (qmi_device_allocate_client_finish (QMI_DEVICE (qmi_device), res, &ctx->error));
	if (ctx->qmi_client == NULL) {
		fu_qmi_pdc_updater_open_abort (ctx);
		return;
	}

	g_main_loop_quit (ctx->mainloop);
}

static void
fu_qmi_pdc_updater_qmi_device_open_ready (GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	if (!qmi_device_open_finish (QMI_DEVICE (qmi_device), res, &ctx->error)) {
		fu_qmi_pdc_updater_open_abort (ctx);
		return;
	}

	qmi_device_allocate_client (ctx->qmi_device, QMI_SERVICE_PDC, QMI_CID_NONE, 5, NULL,
				    fu_qmi_pdc_updater_qmi_device_allocate_client_ready, ctx);
}

static void
fu_qmi_pdc_updater_qmi_device_open_attempt (OpenContext *ctx)
{
	QmiDeviceOpenFlags open_flags = QMI_DEVICE_OPEN_FLAGS_NONE;

	g_debug ("trying to open QMI device...");

	/* automatically detect QMI and MBIM ports */
	open_flags |= QMI_DEVICE_OPEN_FLAGS_AUTO;
	/* qmi pdc requires indications, so enable them by default */
	open_flags |= QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS;
	/* all communication through the proxy */
	open_flags |= QMI_DEVICE_OPEN_FLAGS_PROXY;

	qmi_device_open (ctx->qmi_device, open_flags, 5, NULL,
			 fu_qmi_pdc_updater_qmi_device_open_ready, ctx);
}

static void
fu_qmi_pdc_updater_qmi_device_new_ready (GObject *source, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	ctx->qmi_device = qmi_device_new_finish (res, &ctx->error);
	if (ctx->qmi_device == NULL) {
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	fu_qmi_pdc_updater_qmi_device_open_attempt (ctx);
}

gboolean
fu_qmi_pdc_updater_open (FuQmiPdcUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	g_autoptr(GFile) qmi_device_file = g_file_new_for_path (self->qmi_port);
	OpenContext ctx = {
		.mainloop = mainloop,
		.open_attempts = FU_QMI_PDC_MAX_OPEN_ATTEMPTS,
	};

	qmi_device_new (qmi_device_file, NULL, fu_qmi_pdc_updater_qmi_device_new_ready, &ctx);
	g_main_loop_run (mainloop);

	/* either we have all device, client and config list  set, or otherwise error is set */

	if ((ctx.qmi_device != NULL) && (ctx.qmi_client != NULL)) {
		g_assert (!ctx.error);
		self->qmi_device = ctx.qmi_device;
		self->qmi_client = ctx.qmi_client;
		/* success */
		return TRUE;
	}

	g_assert (ctx.error != NULL);
	g_warning ("open failed: %s", ctx.error->message);

	g_assert (ctx.qmi_device == NULL);
	g_assert (ctx.qmi_client == NULL);
	g_propagate_error (error, ctx.error);
	return FALSE;
}

typedef struct {
	GMainLoop	   *mainloop;
	QmiDevice	   *qmi_device;
	QmiClientPdc	   *qmi_client;
	GError		   *error;
} CloseContext;

static void
fu_qmi_pdc_updater_qmi_device_close_ready (GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	CloseContext *ctx = (CloseContext *) user_data;

	/* ignore errors when closing if we had one already set when releasing client */
	qmi_device_close_finish (QMI_DEVICE (qmi_device), res, (ctx->error == NULL) ? &ctx->error : NULL);
	g_clear_object (&ctx->qmi_device);
	g_main_loop_quit (ctx->mainloop);
}

static void
fu_qmi_pdc_updater_qmi_device_release_client_ready (GObject *qmi_device, GAsyncResult *res, gpointer user_data)
{
	CloseContext *ctx = (CloseContext *) user_data;

	qmi_device_release_client_finish (QMI_DEVICE (qmi_device), res, &ctx->error);
	g_clear_object (&ctx->qmi_client);

	qmi_device_close_async (ctx->qmi_device,
				15, NULL, fu_qmi_pdc_updater_qmi_device_close_ready, ctx);
}

gboolean
fu_qmi_pdc_updater_close (FuQmiPdcUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	CloseContext ctx = {
		.mainloop = mainloop,
		.qmi_device = g_steal_pointer (&self->qmi_device),
		.qmi_client = g_steal_pointer (&self->qmi_client),
	};

	qmi_device_release_client (ctx.qmi_device, QMI_CLIENT (ctx.qmi_client),
				   QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
				   5, NULL, fu_qmi_pdc_updater_qmi_device_release_client_ready, &ctx);
	g_main_loop_run (mainloop);

	/* we should always have both device and client cleared, and optionally error set */

	g_assert (ctx.qmi_device == NULL);
	g_assert (ctx.qmi_client == NULL);

	if (ctx.error != NULL) {
		g_propagate_error (error, ctx.error);
		return FALSE;
	}

	return TRUE;
}

#define QMI_LOAD_CHUNK_SIZE 0x400

typedef struct {
	GMainLoop	*mainloop;
	QmiClientPdc	*qmi_client;
	GError		*error;
	gulong		 indication_id;
	guint		 timeout_id;
	GBytes		*blob;
	GArray		*digest;
	gsize		 offset;
	guint		 token;
} WriteContext;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(QmiMessagePdcLoadConfigInput, qmi_message_pdc_load_config_input_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(QmiMessagePdcLoadConfigOutput, qmi_message_pdc_load_config_output_unref)
#pragma clang diagnostic pop

static void fu_qmi_pdc_updater_load_config (WriteContext *ctx);

static gboolean
fu_qmi_pdc_updater_load_config_timeout (gpointer user_data)
{
	WriteContext *ctx = user_data;

	ctx->timeout_id = 0;
	g_signal_handler_disconnect (ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	g_set_error_literal (&ctx->error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "couldn't load mcfg: timed out");
	g_main_loop_quit (ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static void
fu_qmi_pdc_updater_load_config_indication (QmiClientPdc *client,
					   QmiIndicationPdcLoadConfigOutput *output,
					   WriteContext *ctx)
{
	gboolean frame_reset;
	guint32 remaining_size;
	guint16 error_code = 0;

	g_source_remove (ctx->timeout_id);
	ctx->timeout_id = 0;
	g_signal_handler_disconnect (ctx->qmi_client, ctx->indication_id);
	ctx->indication_id = 0;

	if (!qmi_indication_pdc_load_config_output_get_indication_result (output, &error_code, &ctx->error)) {
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (error_code != 0) {
		/* when a given mcfg file already exists in the device, an "invalid id" error is returned;
		 * the error naming here is a bit off, as the same protocol error number is used both for
		 * 'invalid id' and 'invalid qos id'
		 */
		if (error_code == QMI_PROTOCOL_ERROR_INVALID_QOS_ID) {
			g_debug ("file already available in device");
			g_main_loop_quit (ctx->mainloop);
			return;
		}

		g_set_error (&ctx->error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "couldn't load mcfg: %s", qmi_protocol_error_get_string ((QmiProtocolError) error_code));
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (qmi_indication_pdc_load_config_output_get_frame_reset (output, &frame_reset, NULL) && frame_reset) {
		g_set_error (&ctx->error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "couldn't load mcfg: sent data discarded");
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (!qmi_indication_pdc_load_config_output_get_remaining_size (output, &remaining_size, &ctx->error)) {
		g_prefix_error (&ctx->error, "couldn't load remaining size: ");
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (remaining_size == 0) {
		g_debug ("finished loading mcfg");
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	g_debug ("loading next chunk (%u bytes remaining)", remaining_size);
	fu_qmi_pdc_updater_load_config (ctx);
}

static void
fu_qmi_pdc_updater_load_config_ready (GObject *qmi_client, GAsyncResult *res, gpointer user_data)
{
	WriteContext *ctx = (WriteContext *) user_data;
	g_autoptr(QmiMessagePdcLoadConfigOutput) output = NULL;

	output = qmi_client_pdc_load_config_finish (QMI_CLIENT_PDC (qmi_client), res, &ctx->error);
	if (output == NULL) {
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (!qmi_message_pdc_load_config_output_get_result (output, &ctx->error)) {
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	/* after receiving the response to our request, we now expect an indication
	 * with the actual result of the operation */
	g_assert (ctx->indication_id == 0);
	ctx->indication_id = g_signal_connect (ctx->qmi_client, "load-config",
					       G_CALLBACK (fu_qmi_pdc_updater_load_config_indication), ctx);

	/* don't wait forever */
	g_assert (ctx->timeout_id == 0);
	ctx->timeout_id = g_timeout_add_seconds (5, fu_qmi_pdc_updater_load_config_timeout, ctx);
}

static void
fu_qmi_pdc_updater_load_config (WriteContext *ctx)
{
	g_autoptr(QmiMessagePdcLoadConfigInput) input = NULL;
	g_autoptr(GArray) chunk = NULL;
	gsize full_size;
	gsize chunk_size;

	input = qmi_message_pdc_load_config_input_new ();
	qmi_message_pdc_load_config_input_set_token (input, ctx->token++, NULL);

	full_size = g_bytes_get_size (ctx->blob);
	chunk_size = (((ctx->offset + QMI_LOAD_CHUNK_SIZE) > full_size) ?
		      (full_size - ctx->offset) :
		      QMI_LOAD_CHUNK_SIZE);

	chunk = g_array_sized_new (FALSE, FALSE, sizeof (guint8), chunk_size);
	g_array_set_size (chunk, chunk_size);
	memcpy (chunk->data, (const guint8 *)g_bytes_get_data (ctx->blob, NULL) + ctx->offset, chunk_size);

	qmi_message_pdc_load_config_input_set_config_chunk (input,
							    QMI_PDC_CONFIGURATION_TYPE_SOFTWARE,
							    ctx->digest,
							    full_size,
							    chunk,
							    NULL);
	ctx->offset += chunk_size;

	qmi_client_pdc_load_config (ctx->qmi_client, input, 10, NULL, fu_qmi_pdc_updater_load_config_ready, ctx);
}

gboolean
fu_qmi_pdc_updater_write (FuQmiPdcUpdater *self, const gchar *filename, GBytes *blob, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	g_autoptr(GChecksum) checksum = NULL;
	g_autoptr(GArray) digest = NULL;
	gsize file_size;
	gsize hash_size;
	WriteContext ctx = {
		.mainloop = mainloop,
		.qmi_client = self->qmi_client,
		.blob = blob,
	};

	/* get checksum, to be used as unique id */
	file_size = g_bytes_get_size (ctx.blob);
	hash_size = g_checksum_type_get_length (G_CHECKSUM_SHA1);
	checksum = g_checksum_new (G_CHECKSUM_SHA1);
	g_checksum_update (checksum, g_bytes_get_data (ctx.blob, NULL), file_size);
	/* libqmi expects a GArray of bytes, not a GByteArray */
	digest = g_array_sized_new (FALSE, FALSE, sizeof (guint8), hash_size);
	g_array_set_size (digest, hash_size);
	g_checksum_get_digest (checksum, (guint8 *)digest->data, &hash_size);
	ctx.digest = digest;

	fu_qmi_pdc_updater_load_config (&ctx);
	g_main_loop_run (mainloop);

	if (ctx.error != NULL) {
		g_propagate_error (error, ctx.error);
		return FALSE;
	}

	return TRUE;
}

static void
fu_qmi_pdc_updater_init (FuQmiPdcUpdater *self)
{
}

static void
fu_qmi_pdc_updater_finalize (GObject *object)
{
	FuQmiPdcUpdater *self = FU_QMI_PDC_UPDATER (object);
	g_assert (self->qmi_client == NULL);
	g_assert (self->qmi_device == NULL);
	g_free (self->qmi_port);
	G_OBJECT_CLASS (fu_qmi_pdc_updater_parent_class)->finalize (object);
}

static void
fu_qmi_pdc_updater_class_init (FuQmiPdcUpdaterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_qmi_pdc_updater_finalize;
}

FuQmiPdcUpdater *
fu_qmi_pdc_updater_new (const gchar *path)
{
	FuQmiPdcUpdater *self = g_object_new (FU_TYPE_QMI_PDC_UPDATER, NULL);
	self->qmi_port = g_strdup (path);
	return self;
}
