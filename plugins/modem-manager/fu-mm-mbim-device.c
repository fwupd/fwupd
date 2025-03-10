/*
 * Copyright 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-mbim-common.h"
#include "fu-mm-mbim-device.h"

struct _FuMmMbimDevice {
	FuMmDevice parent_instance;
	MbimDevice *mbim_device;
};

G_DEFINE_TYPE(FuMmMbimDevice, fu_mm_mbim_device, FU_TYPE_MM_DEVICE)

#define FU_MM_MBIM_DEVICE_MAX_OPEN_ATTEMPTS 8

#define FU_MM_MBIM_DEVICE_TIMEOUT_MS 1500

typedef struct {
	gboolean ret;
	GMainLoop *loop;
	MbimDevice *mbim_device;
	GError *error;
} _MbimDeviceHelper;

static void
fu_mm_mbim_device_caps_query_cb(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	_MbimDeviceHelper *helper = user_data;
	g_autofree gchar *firmware_version = NULL;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(device, res, &helper->error);
	if (response == NULL || !mbim_message_response_get_result(response,
								  MBIM_MESSAGE_TYPE_COMMAND_DONE,
								  &helper->error)) {
		g_debug("operation failed: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
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
						     &helper->error)) {
		g_debug("couldn't parse response message: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	g_debug("[%s] Successfully request modem to query caps",
		mbim_device_get_path_display(device));
	g_debug("new firmware version: %s", firmware_version);

	g_main_loop_quit(helper->loop);
}

static gboolean
fu_mm_mbim_device_ensure_firmware_version(FuMmMbimDevice *self, GError **error)
{
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	g_autoptr(MbimMessage) request = NULL;
	_MbimDeviceHelper helper = {
	    .loop = loop,
	    .error = NULL,
	};

	request = mbim_message_device_caps_query_new(NULL);
	mbim_device_command(self->mbim_device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mm_mbim_device_caps_query_cb,
			    &helper);

	g_main_loop_run(loop);
	if (helper.error != NULL) {
		g_propagate_error(error, helper.error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

#if MBIM_CHECK_VERSION(1, 27, 5)
static void
fu_mm_mbim_device_detach_to_edl_cb(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	GMainLoop *loop = user_data;

	/* no need to check for a response since MBIM port goes away without sending one */
	g_main_loop_quit(loop);
}

static gboolean
fu_mm_mbim_device_detach_to_edl(FuMmMbimDevice *self, GError **error)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new(NULL, FALSE);
	g_autoptr(MbimMessage) message = NULL;

	message = mbim_message_qdu_quectel_reboot_set_new(MBIM_QDU_QUECTEL_REBOOT_TYPE_EDL, NULL);
	mbim_device_command(self->mbim_device,
			    message,
			    5,
			    NULL,
			    (GAsyncReadyCallback)fu_mm_mbim_device_detach_to_edl_cb,
			    mainloop);
	g_main_loop_run(mainloop);
	return TRUE;
}
#endif // MBIM_CHECK_VERSION(1, 27, 5)

static gboolean
fu_mm_mbim_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);

	/* use special command for Quectel MBIM devices */
	if (fu_device_get_vid(device) == 0x2C7C) {
#if MBIM_CHECK_VERSION(1, 27, 5)
		if (!fu_mm_mbim_device_detach_to_edl(self, error))
			return FALSE;
#else
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "libmbim >= 1.27.5 required for Quectel MBIM devices");
		return FALSE;
#endif
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

typedef struct {
	FuMmMbimDevice *self; /* noref */
	GMainLoop *loop;
	GError *error;
	GBytes *blob;
	GArray *digest;
	FuChunkArray *chunks;
	guint chunk_sent;
	FuProgress *progress;
} FuMmMbimDeviceWriteHelper;

static void
fu_mm_mbim_device_write_helper_free(FuMmMbimDeviceWriteHelper *helper)
{
	g_main_loop_unref(helper->loop);
	if (helper->error != NULL)
		g_error_free(helper->error);
	if (helper->blob != NULL)
		g_bytes_unref(helper->blob);
	if (helper->digest != NULL)
		g_array_unref(helper->digest);
	if (helper->chunks != NULL)
		g_object_unref(helper->chunks);
	if (helper->progress != NULL)
		g_object_unref(helper->progress);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMmMbimDeviceWriteHelper, fu_mm_mbim_device_write_helper_free)

static void
fu_mm_mbim_device_file_write_cb(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceWriteHelper *helper = user_data;
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(helper->self);
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(device, res, &helper->error);
	if (response == NULL || !mbim_message_response_get_result(response,
								  MBIM_MESSAGE_TYPE_COMMAND_DONE,
								  &helper->error)) {
		g_debug("operation failed: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	if (!mbim_message_qdu_file_write_response_parse(response, &helper->error)) {
		g_debug("couldn't parse response message: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	helper->chunk_sent++;
	fu_progress_set_percentage_full(helper->progress,
					(gsize)helper->chunk_sent,
					(gsize)fu_chunk_array_length(helper->chunks));
	if (helper->chunk_sent < fu_chunk_array_length(helper->chunks)) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(MbimMessage) request = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(helper->chunks, helper->chunk_sent, &helper->error);
		if (chk == NULL) {
			g_main_loop_quit(helper->loop);
			return;
		}
		request =
		    mbim_message_qdu_file_write_set_new(fu_chunk_get_data_sz(chk),
							(const guint8 *)fu_chunk_get_data(chk),
							NULL);
		mbim_device_command(self->mbim_device,
				    request,
				    20,
				    NULL,
				    (GAsyncReadyCallback)fu_mm_mbim_device_file_write_cb,
				    helper);
		return;
	}
	g_main_loop_quit(helper->loop);
}

static void
fu_mm_mbim_device_file_open_cb(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceWriteHelper *helper = user_data;
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(helper->self);
	guint32 out_max_transfer_size;
	g_autoptr(FuChunk) chk = NULL;
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(device, res, &helper->error);
	if (response == NULL || !mbim_message_response_get_result(response,
								  MBIM_MESSAGE_TYPE_COMMAND_DONE,
								  &helper->error)) {
		g_debug("operation failed: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	if (!mbim_message_qdu_file_open_response_parse(response,
						       &out_max_transfer_size,
						       NULL,
						       &helper->error)) {
		g_debug("couldn't parse response message: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	helper->chunks = fu_chunk_array_new_from_bytes(helper->blob,
						       FU_CHUNK_ADDR_OFFSET_NONE,
						       FU_CHUNK_PAGESZ_NONE,
						       out_max_transfer_size);
	chk = fu_chunk_array_index(helper->chunks, 0, &helper->error);
	if (chk == NULL) {
		g_main_loop_quit(helper->loop);
		return;
	}
	request = mbim_message_qdu_file_write_set_new(fu_chunk_get_data_sz(chk),
						      (const guint8 *)fu_chunk_get_data(chk),
						      NULL);
	mbim_device_command(self->mbim_device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mm_mbim_device_file_write_cb,
			    helper);
}

static void
fu_mm_mbim_device_session_ready_cb(MbimDevice *device, GAsyncResult *res, gpointer user_data)
{
	FuMmMbimDeviceWriteHelper *helper = user_data;
	g_autoptr(MbimMessage) response = NULL;
	g_autoptr(MbimMessage) request = NULL;

	response = mbim_device_command_finish(device, res, &helper->error);
	if (response == NULL || !mbim_message_response_get_result(response,
								  MBIM_MESSAGE_TYPE_COMMAND_DONE,
								  &helper->error)) {
		g_debug("operation failed: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	if (!mbim_message_qdu_update_session_response_parse(response,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    NULL,
							    &helper->error)) {
		g_debug("couldn't parse response message: %s", helper->error->message);
		g_main_loop_quit(helper->loop);
		return;
	}

	g_debug("[%s] Successfully request modem to update session",
		mbim_device_get_path_display(device));

	request = mbim_message_qdu_file_open_set_new(MBIM_QDU_FILE_TYPE_LITTLE_ENDIAN_PACKAGE,
						     g_bytes_get_size(helper->blob),
						     NULL);
	mbim_device_command(device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mm_mbim_device_file_open_cb,
			    helper);
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
fu_mm_mbim_device_write(FuMmMbimDevice *self,
			const gchar *filename,
			GBytes *blob,
			FuProgress *progress,
			GError **error)
{
	g_autoptr(FuMmMbimDeviceWriteHelper) helper = g_new0(FuMmMbimDeviceWriteHelper, 1);
	g_autoptr(MbimMessage) request = NULL;

	/* use a helper to perform the linked async actions */
	helper->self = self;
	helper->loop = g_main_loop_new(NULL, FALSE);
	helper->blob = g_bytes_ref(blob);
	helper->progress = g_object_ref(progress);
	helper->digest = fu_mm_mbim_device_get_checksum(blob); // fixme rename checksum

	/* start update */
	request = mbim_message_qdu_update_session_set_new(MBIM_QDU_SESSION_ACTION_START,
							  MBIM_QDU_SESSION_TYPE_LE,
							  NULL);
	mbim_device_command(self->mbim_device,
			    request,
			    10,
			    NULL,
			    (GAsyncReadyCallback)fu_mm_mbim_device_session_ready_cb,
			    helper);
	g_main_loop_run(helper->loop);
	if (helper->error != NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}

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
	return _mbim_device_open_sync(self->mbim_device, FU_MM_MBIM_DEVICE_TIMEOUT_MS, error);
}

static gboolean
fu_mm_mbim_device_open(FuDevice *device, GError **error)
{
	FuMmMbimDevice *self = FU_MM_MBIM_DEVICE(device);
	g_autoptr(GFile) mbim_device_file =
	    g_file_new_for_path(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)));

	/* create and open */
	g_clear_object(&self->mbim_device);
	self->mbim_device =
	    _mbim_device_new_sync(mbim_device_file, FU_MM_MBIM_DEVICE_TIMEOUT_MS, error);
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
	g_autoptr(MbimDevice) mbim_device = NULL;

	/* sanity check */
	if (self->mbim_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no mbim_device");
		return FALSE;
	}
	mbim_device = g_steal_pointer(&self->mbim_device);
	return _mbim_device_close_sync(mbim_device, FU_MM_MBIM_DEVICE_TIMEOUT_MS, error);
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
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.mbim_qdu");
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
