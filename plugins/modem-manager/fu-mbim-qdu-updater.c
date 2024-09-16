/*
 * Copyright 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "fu-mbim-qdu-updater.h"

#define FU_MBIM_QDU_MAX_OPEN_ATTEMPTS 8

struct _FuMbimQduUpdater {
	GObject parent_instance;
	gchar *mbim_port;
	MbimDevice *mbim_device;
};

G_DEFINE_TYPE(FuMbimQduUpdater, fu_mbim_qdu_updater, G_TYPE_OBJECT)

typedef struct {
	GMainLoop *mainloop;
	MbimDevice *mbim_device;
	GError *error;
	guint open_attempts;
} OpenContext;

static void
fu_mbim_qdu_updater_mbim_device_open_attempt(OpenContext *ctx);

static void
fu_mbim_qdu_updater_mbim_device_open_ready(GObject *mbim_device,
					   GAsyncResult *res,
					   gpointer user_data)
{
	OpenContext *ctx = (OpenContext *)user_data;

	if (ctx->open_attempts == 0) {
		g_set_error_literal(&ctx->error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no open attempts");
		return;
	}

	if (!mbim_device_open_full_finish(MBIM_DEVICE(mbim_device), res, &ctx->error)) {
		ctx->open_attempts--;
		if (ctx->open_attempts == 0) {
			g_clear_object(&ctx->mbim_device);
			g_main_loop_quit(ctx->mainloop);
			return;
		}

		/* retry */
		g_debug("couldn't open mbim device: %s", ctx->error->message);
		g_clear_error(&ctx->error);
		fu_mbim_qdu_updater_mbim_device_open_attempt(ctx);
		return;
	}

	g_main_loop_quit(ctx->mainloop);
}

static void
fu_mbim_qdu_updater_mbim_device_open_attempt(OpenContext *ctx)
{
	/* all communication through the proxy */
	MbimDeviceOpenFlags open_flags = MBIM_DEVICE_OPEN_FLAGS_PROXY;

	g_debug("trying to open MBIM device...");
	mbim_device_open_full(ctx->mbim_device,
			      open_flags,
			      10,
			      NULL,
			      fu_mbim_qdu_updater_mbim_device_open_ready,
			      ctx);
}

static void
fu_mbim_qdu_updater_mbim_device_new_ready(GObject *source, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *)user_data;

	ctx->mbim_device = mbim_device_new_finish(res, &ctx->error);
	if (ctx->mbim_device == NULL) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	fu_mbim_qdu_updater_mbim_device_open_attempt(ctx);
}

gboolean
fu_mbim_qdu_updater_open(FuMbimQduUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	g_autoptr(GFile) mbim_device_file = g_file_new_for_path(self->mbim_port);
	OpenContext ctx = {
	    .mainloop = mainloop,
	    .mbim_device = NULL,
	    .error = NULL,
	    .open_attempts = FU_MBIM_QDU_MAX_OPEN_ATTEMPTS,
	};

	mbim_device_new(mbim_device_file, NULL, fu_mbim_qdu_updater_mbim_device_new_ready, &ctx);
	g_main_loop_run(mainloop);

	/* either we have all device or otherwise error is set */
	if (ctx.mbim_device != NULL) {
		g_warn_if_fail(ctx.error == NULL);
		self->mbim_device = ctx.mbim_device;
		/* success */
		return TRUE;
	}

	g_warn_if_fail(ctx.error != NULL);
	g_warn_if_fail(ctx.mbim_device == NULL);
	g_propagate_error(error, ctx.error);
	return FALSE;
}

typedef struct {
	GMainLoop *mainloop;
	MbimDevice *mbim_device;
	GError *error;
} CloseContext;

static void
fu_mbim_qdu_updater_mbim_device_close_ready(GObject *mbim_device,
					    GAsyncResult *res,
					    gpointer user_data)
{
	CloseContext *ctx = (CloseContext *)user_data;

	/* ignore errors when closing */
	mbim_device_close_finish(MBIM_DEVICE(mbim_device), res, &ctx->error);
	g_clear_object(&ctx->mbim_device);
	g_main_loop_quit(ctx->mainloop);
}

gboolean
fu_mbim_qdu_updater_close(FuMbimQduUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	CloseContext ctx = {
	    .mainloop = mainloop,
	    .mbim_device = g_steal_pointer(&self->mbim_device),
	    .error = NULL,
	};

	if (ctx.mbim_device == NULL)
		return TRUE;

	mbim_device_close(ctx.mbim_device,
			  5,
			  NULL,
			  fu_mbim_qdu_updater_mbim_device_close_ready,
			  &ctx);
	g_main_loop_run(mainloop);

	/* we should always have both device cleared, and optionally error set */
	g_warn_if_fail(ctx.mbim_device == NULL);

	if (ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return FALSE;
	}

	/* update attach right after this */
	return TRUE;
}

typedef struct {
	GMainLoop *mainloop;
	GError *error;
	gchar *firmware_version;
} GetFirmwareVersionContext;

static void
fu_mbim_qdu_updater_caps_query_ready(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	GetFirmwareVersionContext *ctx = user_data;
	g_autofree gchar *firmware_version = NULL;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(device, res, &ctx->error);
	if (!response || !mbim_message_response_get_result(response,
							   MBIM_MESSAGE_TYPE_COMMAND_DONE,
							   &ctx->error)) {
		g_debug("operation failed: %s", ctx->error->message);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

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
						     &ctx->error)) {
		g_debug("couldn't parse response message: %s", ctx->error->message);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	g_debug("[%s] Successfully request modem to query caps",
		mbim_device_get_path_display(device));

	ctx->firmware_version = g_strdup(firmware_version);

	g_main_loop_quit(ctx->mainloop);
}

static void
fu_mbim_qdu_updater_caps_query(MbimDevice *device, GetFirmwareVersionContext *ctx)
{
	g_autoptr(MbimMessage) request = NULL;

	request = mbim_message_device_caps_query_new(NULL);

	mbim_device_command(device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mbim_qdu_updater_caps_query_ready,
			    ctx);
}

gchar *
fu_mbim_qdu_updater_check_ready(FuMbimQduUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	GetFirmwareVersionContext ctx = {
	    .mainloop = mainloop,
	    .error = NULL,
	    .firmware_version = NULL,
	};

	fu_mbim_qdu_updater_caps_query(self->mbim_device, &ctx);

	g_main_loop_run(mainloop);

	if (ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return NULL;
	}

	return ctx.firmware_version;
}

typedef struct {
	GMainLoop *mainloop;
	MbimDevice *mbim_device;
	GError *error;
	GBytes *blob;
	GArray *digest;
	FuChunkArray *chunks;
	guint chunk_sent;
	FuDevice *device;
	FuProgress *progress;
} WriteContext;

static void
fu_mbim_qdu_updater_file_write_ready(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	WriteContext *ctx = user_data;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(device, res, &ctx->error);
	if (!response || !mbim_message_response_get_result(response,
							   MBIM_MESSAGE_TYPE_COMMAND_DONE,
							   &ctx->error)) {
		g_debug("operation failed: %s", ctx->error->message);
		g_object_unref(ctx->chunks);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!mbim_message_qdu_file_write_response_parse(response, &ctx->error)) {
		g_debug("couldn't parse response message: %s", ctx->error->message);
		g_object_unref(ctx->chunks);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	ctx->chunk_sent++;
	fu_progress_set_percentage_full(ctx->progress,
					(gsize)ctx->chunk_sent,
					(gsize)fu_chunk_array_length(ctx->chunks));
	if (ctx->chunk_sent < fu_chunk_array_length(ctx->chunks)) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(MbimMessage) request = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(ctx->chunks, ctx->chunk_sent, &ctx->error);
		if (chk == NULL) {
			g_main_loop_quit(ctx->mainloop);
			return;
		}
		request =
		    mbim_message_qdu_file_write_set_new(fu_chunk_get_data_sz(chk),
							(const guint8 *)fu_chunk_get_data(chk),
							NULL);
		mbim_device_command(ctx->mbim_device,
				    request,
				    20,
				    NULL,
				    (GAsyncReadyCallback)fu_mbim_qdu_updater_file_write_ready,
				    ctx);
		return;
	}

	g_object_unref(ctx->chunks);
	g_main_loop_quit(ctx->mainloop);
}

static void
fu_mbim_qdu_updater_file_open_ready(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	WriteContext *ctx = user_data;
	guint32 out_max_transfer_size;
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(device, res, &ctx->error);
	if (!response || !mbim_message_response_get_result(response,
							   MBIM_MESSAGE_TYPE_COMMAND_DONE,
							   &ctx->error)) {
		g_debug("operation failed: %s", ctx->error->message);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!mbim_message_qdu_file_open_response_parse(response,
						       &out_max_transfer_size,
						       NULL,
						       &ctx->error)) {
		g_debug("couldn't parse response message: %s", ctx->error->message);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	ctx->chunks = fu_chunk_array_new_from_bytes(ctx->blob, 0x00, out_max_transfer_size);
	chk = fu_chunk_array_index(ctx->chunks, 0, &ctx->error);
	if (chk == NULL) {
		g_main_loop_quit(ctx->mainloop);
		return;
	}
	request = mbim_message_qdu_file_write_set_new(fu_chunk_get_data_sz(chk),
						      (const guint8 *)fu_chunk_get_data(chk),
						      NULL);
	mbim_device_command(ctx->mbim_device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mbim_qdu_updater_file_write_ready,
			    ctx);
}

static void
fu_mbim_qdu_updater_session_ready(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	WriteContext *ctx = user_data;
	g_autoptr(MbimMessage) response = NULL;
	g_autoptr(MbimMessage) request = NULL;

	response = mbim_device_command_finish(device, res, &ctx->error);
	if (!response || !mbim_message_response_get_result(response,
							   MBIM_MESSAGE_TYPE_COMMAND_DONE,
							   &ctx->error)) {
		g_debug("operation failed: %s", ctx->error->message);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	if (!mbim_message_qdu_update_session_response_parse(response,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    &ctx->error)) {
		g_debug("couldn't parse response message: %s", ctx->error->message);
		g_main_loop_quit(ctx->mainloop);
		return;
	}

	g_debug("[%s] Successfully request modem to update session",
		mbim_device_get_path_display(device));

	request = mbim_message_qdu_file_open_set_new(MBIM_QDU_FILE_TYPE_LITTLE_ENDIAN_PACKAGE,
						     g_bytes_get_size(ctx->blob),
						     NULL);
	mbim_device_command(device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mbim_qdu_updater_file_open_ready,
			    ctx);
}

static void
fu_mbim_qdu_updater_set_update_session(MbimDevice *device, WriteContext *ctx)
{
	g_autoptr(MbimMessage) request = NULL;

	request = mbim_message_qdu_update_session_set_new(MBIM_QDU_SESSION_ACTION_START,
							  MBIM_QDU_SESSION_TYPE_LE,
							  NULL);

	mbim_device_command(device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mbim_qdu_updater_session_ready,
			    ctx);
}

static GArray *
fu_mbim_qdu_updater_get_checksum(GBytes *blob)
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

GArray *
fu_mbim_qdu_updater_write(FuMbimQduUpdater *self,
			  const gchar *filename,
			  GBytes *blob,
			  FuDevice *device,
			  FuProgress *progress,
			  GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	g_autoptr(GArray) digest = fu_mbim_qdu_updater_get_checksum(blob);
	g_autoptr(FuChunkArray) chunks = NULL;
	WriteContext ctx = {
	    .mainloop = mainloop,
	    .mbim_device = self->mbim_device,
	    .error = NULL,
	    .blob = blob,
	    .digest = digest,
	    .chunks = chunks,
	    .chunk_sent = 0,
	    .device = device,
	    .progress = progress,
	};

	fu_mbim_qdu_updater_set_update_session(self->mbim_device, &ctx);

	g_main_loop_run(mainloop);

	if (ctx.error != NULL) {
		g_propagate_error(error, ctx.error);
		return NULL;
	}

	return g_steal_pointer(&digest);
}

MbimDevice *
fu_mbim_qdu_updater_get_mbim_device(FuMbimQduUpdater *self)
{
	return self->mbim_device;
}

static void
fu_mbim_qdu_updater_init(FuMbimQduUpdater *self)
{
}

static void
fu_mbim_qdu_updater_finalize(GObject *object)
{
	FuMbimQduUpdater *self = FU_MBIM_QDU_UPDATER(object);
	g_warn_if_fail(self->mbim_device == NULL);
	g_free(self->mbim_port);
	G_OBJECT_CLASS(fu_mbim_qdu_updater_parent_class)->finalize(object);
}

static void
fu_mbim_qdu_updater_class_init(FuMbimQduUpdaterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_mbim_qdu_updater_finalize;
}

FuMbimQduUpdater *
fu_mbim_qdu_updater_new(const gchar *mbim_port)
{
	FuMbimQduUpdater *self = g_object_new(FU_TYPE_MBIM_QDU_UPDATER, NULL);
	self->mbim_port = g_strdup(mbim_port);
	return self;
}
