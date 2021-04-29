/*
 * Copyright (C) 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#include "fu-common.h"
#include "fu-mbim-qdu-updater.h"
#include "fu-chunk.h"
#include "fu-mm-utils.h"

#define FU_MBIM_QDU_MAX_OPEN_ATTEMPTS 8

struct _FuMbimQduUpdater {
	GObject		 parent_instance;
	gchar		*mbim_port;
	MbimDevice	*mbim_device;
};

G_DEFINE_TYPE (FuMbimQduUpdater, fu_mbim_qdu_updater, G_TYPE_OBJECT)

typedef struct {
	GMainLoop	*mainloop;
	MbimDevice	*mbim_device;
	GError		*error;
	guint		 open_attempts;
} OpenContext;

static void fu_mbim_qdu_updater_mbim_device_open_attempt (OpenContext *ctx);

static void
fu_mbim_qdu_updater_mbim_device_open_abort_ready (GObject *mbim_device, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	g_warn_if_fail (ctx->error != NULL);

	/* ignore errors when aborting open */
	mbim_device_close_finish (MBIM_DEVICE (mbim_device), res, NULL);

	ctx->open_attempts--;
	if (ctx->open_attempts == 0) {
		g_clear_object (&ctx->mbim_device);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	/* retry */
	g_clear_error (&ctx->error);
	fu_mbim_qdu_updater_mbim_device_open_attempt (ctx);
}

static void
fu_mbim_qdu_updater_open_abort (OpenContext *ctx)
{
	mbim_device_close (ctx->mbim_device,
				10, NULL, fu_mbim_qdu_updater_mbim_device_open_abort_ready, ctx);
}

static void
fu_mbim_qdu_updater_mbim_device_open_ready (GObject *mbim_device, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	if (!mbim_device_open_finish (MBIM_DEVICE (mbim_device), res, &ctx->error)) {
		fu_mbim_qdu_updater_open_abort (ctx);
		return;
	}

	g_main_loop_quit (ctx->mainloop);
}

static void
fu_mbim_qdu_updater_mbim_device_open_attempt (OpenContext *ctx)
{
	MbimDeviceOpenFlags open_flags = MBIM_DEVICE_OPEN_FLAGS_NONE;

	/* all communication through the proxy */
	open_flags |= MBIM_DEVICE_OPEN_FLAGS_PROXY;

	g_debug ("trying to open MBIM device...");
	mbim_device_open_full (ctx->mbim_device, open_flags, 10, NULL,
			 fu_mbim_qdu_updater_mbim_device_open_ready, ctx);
}

static void
fu_mbim_qdu_updater_mbim_device_new_ready (GObject *source, GAsyncResult *res, gpointer user_data)
{
	OpenContext *ctx = (OpenContext *) user_data;

	ctx->mbim_device = mbim_device_new_finish (res, &ctx->error);
	if (ctx->mbim_device == NULL) {
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	fu_mbim_qdu_updater_mbim_device_open_attempt (ctx);
}

gboolean
fu_mbim_qdu_updater_open (FuMbimQduUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	g_autoptr(GFile) mbim_device_file = g_file_new_for_path (self->mbim_port);
	OpenContext ctx = {
		.mainloop = mainloop,
		.mbim_device = NULL,
		.error = NULL,
		.open_attempts = FU_MBIM_QDU_MAX_OPEN_ATTEMPTS,
	};

	mbim_device_new (mbim_device_file, NULL, fu_mbim_qdu_updater_mbim_device_new_ready, &ctx);
	g_main_loop_run (mainloop);

	/* either we have all device or otherwise error is set */
	if (ctx.mbim_device != NULL) {
		g_warn_if_fail (!ctx.error);
		self->mbim_device = ctx.mbim_device;
		/* success */
		return TRUE;
	}

	g_warn_if_fail (ctx.error != NULL);
	g_warn_if_fail (ctx.mbim_device == NULL);
	g_propagate_error (error, ctx.error);
	return FALSE;
}

typedef struct {
	GMainLoop	*mainloop;
	MbimDevice	*mbim_device;
	guint		 timeout_id;
	GError		*error;
} CloseContext;

static void
fu_mbim_qdu_updater_mbim_device_close_ready (GObject *mbim_device, GAsyncResult *res, gpointer user_data)
{
	CloseContext *ctx = (CloseContext *) user_data;

	/* ignore errors when closing */
	mbim_device_close_finish (MBIM_DEVICE (mbim_device), res, (ctx->error == NULL) ? &ctx->error : NULL);
	g_clear_object (&ctx->mbim_device);
	g_main_loop_quit (ctx->mainloop);
}

static gboolean
fu_mbim_qdu_updater_reprobe_timeout (gpointer user_data)
{
	CloseContext *ctx = user_data;

	ctx->timeout_id = 0;
	g_main_loop_quit (ctx->mainloop);

	return G_SOURCE_REMOVE;
}

gboolean
fu_mbim_qdu_updater_close (FuMbimQduUpdater *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	g_autofree gchar *device_sysfs_path = NULL;
	CloseContext ctx = {
		.mainloop = mainloop,
		.mbim_device = g_steal_pointer (&self->mbim_device),
		.timeout_id = 0,
	};

	mbim_device_close (ctx.mbim_device, 5, NULL, fu_mbim_qdu_updater_mbim_device_close_ready, &ctx);
	g_main_loop_run (mainloop);

	/* we should always have both device cleared, and optionally error set */

	g_warn_if_fail (ctx.mbim_device == NULL);

	if (ctx.error != NULL) {
		g_propagate_error (error, ctx.error);
		return FALSE;
	}
 
	fu_mm_utils_get_port_info (self->mbim_port, &device_sysfs_path, NULL, NULL);
	/* device will auto reboot after update */
	/* pcie rescan is required */
	/* mbim-proxy stuck due to mbim port suddenly lost */
	if (device_sysfs_path) {
		g_auto(GStrv) split = NULL;
		split = g_strsplit (device_sysfs_path, "/", -1);
		/* /sys/devices/pci0000:00/0000:00:1d.3/0000:02:00.0 */
		if (g_strv_length (split) == 6) {
			const gchar *remove_path = g_strdup_printf ("/sys/bus/pci/devices/%s/%s/remove", split[4], split[5]);
			const gchar *rescan_path = "/sys/bus/pci/rescan";
			gchar value;
			FILE *f;
			g_auto(GStrv) argc = NULL;
			GError *tmp_error = NULL;
			gint exit_stat = 0;

			if (!(f = fopen (remove_path, "w"))) {
				g_debug ("fail to open %s", remove_path);
			}

			if (f) {
				value = '1';
				if (fwrite (&value, 1, 1, f) != 1) {
					g_debug ("fail to write %s", remove_path);
				}
			}

			if (f)
				fclose(f);

			argc = g_new0 (gchar *, 4);
			argc[3] = NULL;
			argc[0] = g_strdup ("killall");
			argc[1] = g_strdup ("-9");
			argc[2] = g_strdup ("mbim-proxy");
			if (!g_spawn_sync (NULL,
					  argc,
					  NULL,
					  G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
					  NULL,
					  NULL,
					  NULL,
					  NULL,
					  &exit_stat,
					  &tmp_error)) {
				g_debug ("error spawning kill mbim-proxy: %s ", tmp_error->message);
				g_clear_error (&tmp_error);
			}

			if (!(f = fopen (rescan_path, "w"))) {
				g_debug ("fail to open %s", rescan_path);
			}

			if (f) {
				value = '1';
				if (fwrite (&value, 1, 1, f) != 1) {
					g_debug ("fail to write %s", rescan_path);
				}
			}

			if (f)
				fclose (f);
			g_debug ("%s", remove_path);
			g_debug ("%s", rescan_path);
		} else
			return FALSE;
	}

	g_warn_if_fail (ctx.timeout_id == 0);
	ctx.timeout_id = g_timeout_add_seconds (15, fu_mbim_qdu_updater_reprobe_timeout, &ctx);
	g_main_loop_run (mainloop);

	//update detach right after this
	return TRUE;
}

typedef struct {
	GMainLoop	*mainloop;
	MbimDevice	*mbim_device;
  	GCancellable    *cancellable;
	GError		*error;
	guint		 timeout_id;
	GBytes		*blob;
	GArray		*digest;
	GPtrArray	*chunks;
	guint 		chunk_sent;
	FuDevice 	*device;
} WriteContext;

static gboolean
fu_mbim_qdu_updater_reboot_timeout (gpointer user_data)
{
	WriteContext *ctx = user_data;

	ctx->timeout_id = 0;
	g_main_loop_quit (ctx->mainloop);

	return G_SOURCE_REMOVE;
}

static void
fu_mbim_qdu_updater_file_write_ready (MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	MbimMessage *request  = NULL;
	MbimMessage *response = NULL;
	g_autoptr(GError)      error = NULL;
	WriteContext *ctx = user_data;

	response = mbim_device_command_finish (device, res, &error);
	if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
		g_debug ("error: operation failed: %s\n", error->message);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (!mbim_message_qdu_file_write_response_parse (
							 response,
							 &error)) {
		g_debug ("[%s][%d]: error: couldn't parse response message: %s\n", __FUNCTION__, __LINE__, error->message);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (response)
		mbim_message_unref (response);

	fu_device_set_progress_full (FU_DEVICE (ctx->device), (gsize) ctx->chunk_sent, (gsize) ctx->chunks->len);
	ctx->chunk_sent++;
	if (ctx->chunk_sent < ctx->chunks->len) {
		FuChunk *chk = g_ptr_array_index (ctx->chunks, ctx->chunk_sent);
		request = (mbim_message_qdu_file_write_set_new (fu_chunk_get_data_sz (chk), (const guint8 *)fu_chunk_get_data (chk), &error));
		mbim_device_command (ctx->mbim_device,
				     request,
				     20,
				     ctx->cancellable,
				     (GAsyncReadyCallback)fu_mbim_qdu_updater_file_write_ready,
				     ctx);
		mbim_message_unref (request);
	} else {
		fu_device_set_progress (FU_DEVICE (ctx->device), 100);
		g_debug ("Done!");
		fu_device_set_status (FU_DEVICE (ctx->device), FWUPD_STATUS_DEVICE_RESTART);
		g_warn_if_fail (ctx->timeout_id == 0);
		/* device will auto reboot right after update finish */
		ctx->timeout_id = g_timeout_add_seconds (30, fu_mbim_qdu_updater_reboot_timeout, ctx);
	}
}

static void
fu_mbim_qdu_updater_file_open_ready (MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	MbimMessage *request = NULL;
	MbimMessage *response = NULL;
	g_autoptr(GError)      error = NULL;
	WriteContext *ctx = user_data;
	guint32  out_max_transfer_size;
	guint32  out_max_window_size;
	guint32  total_sending_numbers = 0;
	FuChunk *chk = NULL;

	response = mbim_device_command_finish (device, res, &error);
	if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
		g_debug ("error: operation failed: %s\n", error->message);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (!mbim_message_qdu_file_open_response_parse (
							response,
							&out_max_transfer_size,
							&out_max_window_size,
							&error)) {
		g_debug ("error: couldn't parse response message: %s\n", error->message);
		g_debug ("out_max_transfer_size: %d\n", (int)out_max_transfer_size);
		g_debug ("out_max_window_size: %d\n", (int)out_max_window_size);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (response)
		mbim_message_unref (response);


	total_sending_numbers = (g_bytes_get_size (ctx->blob)) / (out_max_transfer_size);
	if ((g_bytes_get_size (ctx->blob)) % (out_max_transfer_size) != 0)
		total_sending_numbers ++;

	ctx->chunks = fu_chunk_array_new_from_bytes (ctx->blob,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						out_max_transfer_size);
	chk = g_ptr_array_index (ctx->chunks, 0);
	request = (mbim_message_qdu_file_write_set_new (fu_chunk_get_data_sz (chk), (const guint8 *)fu_chunk_get_data (chk), &error));
	mbim_device_command (ctx->mbim_device,
			     request,
			     10,
			     ctx->cancellable,
			     (GAsyncReadyCallback)fu_mbim_qdu_updater_file_write_ready,
			     ctx);
	mbim_message_unref (request);
}

static void
fu_mbim_qdu_updater_session_ready (MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	MbimMessage *response = NULL;
	MbimQduSessionType     out_current_session_type;
	MbimQduSessionStatus   out_current_session_status;
	MbimQduSessionType     out_last_session_type;
	MbimQduSessionResult   out_last_session_result;
	guint32                out_last_session_error_offset;
	guint32                out_last_session_error_size;
	g_autoptr(GError)      error = NULL;
	MbimMessage *request = NULL;
	WriteContext *ctx = user_data;

	response = mbim_device_command_finish (device, res, &error);
	if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
		g_printerr ("error: operation failed: %s\n", error->message);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (!mbim_message_qdu_update_session_response_parse (
							     response,
							     &out_current_session_type,
							     &out_current_session_status,
							     &out_last_session_type,
							     &out_last_session_result,
							     &out_last_session_error_offset,
							     &out_last_session_error_size,
							     &error)) {
		g_printerr ("error: couldn't parse response message: %s\n", error->message);
		g_printerr ("out_current_session_type: %d\n", (int)out_current_session_type);
		g_printerr ("out_current_session_status: %d\n", (int)out_current_session_status);
		g_printerr ("out_last_session_type: %d\n", (int)out_last_session_type);
		g_printerr ("out_last_session_result: %d\n", (int)out_last_session_result);
		g_printerr ("out_last_session_error_offset: %d\n", (int)out_last_session_error_offset);
		g_printerr ("out_last_session_error_size: %d\n", (int)out_last_session_error_size);
		g_main_loop_quit (ctx->mainloop);
		return;
	}

	if (response)
		mbim_message_unref (response);

	g_debug ("[%s] Successfully request modem to update session", mbim_device_get_path_display (device));

	request = (mbim_message_qdu_file_open_set_new (0, g_bytes_get_size (ctx->blob), &error));
	mbim_device_command (device,
			     request,
			     10,
			     ctx->cancellable,
			     (GAsyncReadyCallback)fu_mbim_qdu_updater_file_open_ready,
			     ctx);
	mbim_message_unref (request);
}

static void
fu_mbim_qdu_updater_set_update_session (MbimDevice *device, WriteContext *ctx)
{
  	MbimMessage *request = NULL;

	request = (mbim_message_qdu_update_session_set_new (0, 1, &ctx->error));
	if (!request)
		g_printerr ("error: couldn't create request: \n");
	else
		g_debug ("Create request");
	
	mbim_device_command (device,
                             request,
                             10,
                             ctx->cancellable,
                             (GAsyncReadyCallback)fu_mbim_qdu_updater_session_ready,
                             ctx);
	mbim_message_unref (request);
}

static GArray *
fu_mbim_qdu_updater_get_checksum (GBytes *blob)
{
	gsize file_size;
	gsize hash_size;
	GArray *digest;
	g_autoptr(GChecksum) checksum = NULL;

	/* get checksum, to be used as unique id */
	file_size = g_bytes_get_size (blob);
	hash_size = g_checksum_type_get_length (G_CHECKSUM_SHA1);
	checksum = g_checksum_new (G_CHECKSUM_SHA1);
	g_checksum_update (checksum, g_bytes_get_data (blob, NULL), file_size);
	
	/* libqmi expects a GArray of bytes, not a GByteArray */
	digest = g_array_sized_new (FALSE, FALSE, sizeof (guint8), hash_size);
	g_array_set_size (digest, hash_size);
	g_checksum_get_digest (checksum, (guint8 *)digest->data, &hash_size);

	return digest;
}

GArray *
fu_mbim_qdu_updater_write (FuMbimQduUpdater *self, const gchar *filename, GBytes *blob, GError **error, FuDevice *device)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	g_autoptr(GArray) digest = fu_mbim_qdu_updater_get_checksum (blob);
	WriteContext ctx = {
		.mainloop = mainloop,
		.mbim_device = self->mbim_device,
		.cancellable = g_cancellable_new (),
		.error = NULL,
		.timeout_id = 0,
		.blob = blob,
		.digest = digest,
		.chunks = NULL,
		.chunk_sent = 0,
		.device = device,
	};

	fu_mbim_qdu_updater_set_update_session (self->mbim_device, &ctx);
	g_main_loop_run (mainloop);

	if (ctx.error != NULL) {
		g_propagate_error (error, ctx.error);
		return NULL;
	}

	return g_steal_pointer (&digest);
}

static void
fu_mbim_qdu_updater_init (FuMbimQduUpdater *self)
{
}

static void
fu_mbim_qdu_updater_finalize (GObject *object)
{
	FuMbimQduUpdater *self = FU_MBIM_QDU_UPDATER (object);
	g_warn_if_fail (self->mbim_device == NULL);
	g_free (self->mbim_port);
	G_OBJECT_CLASS (fu_mbim_qdu_updater_parent_class)->finalize (object);
}

static void
fu_mbim_qdu_updater_class_init (FuMbimQduUpdaterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = fu_mbim_qdu_updater_finalize;
}

FuMbimQduUpdater *
fu_mbim_qdu_updater_new (const gchar *path)
{
	FuMbimQduUpdater *self = g_object_new (FU_TYPE_MBIM_QDU_UPDATER, NULL);
	self->mbim_port = g_strdup (path);
	return self;
}
