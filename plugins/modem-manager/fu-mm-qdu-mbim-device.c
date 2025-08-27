/*
 * Copyright 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-qdu-mbim-device.h"

struct _FuMmQduMbimDevice {
	FuMmMbimDevice parent_instance;
};

G_DEFINE_TYPE(FuMmQduMbimDevice, fu_mm_qdu_mbim_device, FU_TYPE_MM_MBIM_DEVICE)

static gboolean
fu_mm_qdu_mbim_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* to override FuMmMbimDevice->detach */
	return TRUE;
}

static GArray *
fu_mm_qdu_mbim_device_get_checksum(GBytes *blob)
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
fu_mm_qdu_mbim_device_write_chunk(FuMmQduMbimDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	request = mbim_message_qdu_file_write_set_new(fu_chunk_get_data_sz(chk),
						      fu_chunk_get_data(chk),
						      NULL);
	response =
	    fu_mm_mbim_device_command_sync(FU_MM_MBIM_DEVICE(self), request, 20 * 1000, error);
	if (response == NULL)
		return FALSE;
	if (!mbim_message_qdu_file_write_response_parse(response, error)) {
		fu_mm_mbim_device_error_convert(error);
		g_prefix_error_literal(error, "failed to parse write-chunk response: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qdu_mbim_device_write_chunks(FuMmQduMbimDevice *self,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL) {
			g_prefix_error_literal(error, "failed to get chunk: ");
			return FALSE;
		}
		if (!fu_mm_qdu_mbim_device_write_chunk(self, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qdu_mbim_device_write(FuMmQduMbimDevice *self,
			    const gchar *filename,
			    GBytes *blob,
			    FuProgress *progress,
			    GError **error)
{
	guint32 out_max_transfer_size = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GArray) checksum = fu_mm_qdu_mbim_device_get_checksum(blob);
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
	action_start_res = fu_mm_mbim_device_command_sync(FU_MM_MBIM_DEVICE(self),
							  action_start_req,
							  10 * 1000,
							  error);
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
		g_prefix_error_literal(error, "failed to parse action-start response: ");
		return FALSE;
	}
	g_debug("successfully request modem to update session");
	fu_progress_step_done(progress);

	/* get the max transfer size */
	file_open_req = mbim_message_qdu_file_open_set_new(MBIM_QDU_FILE_TYPE_LITTLE_ENDIAN_PACKAGE,
							   g_bytes_get_size(blob),
							   NULL);
	file_open_res = fu_mm_mbim_device_command_sync(FU_MM_MBIM_DEVICE(self),
						       file_open_req,
						       10 * 1000,
						       error);
	if (file_open_res == NULL)
		return FALSE;

	if (!mbim_message_qdu_file_open_response_parse(file_open_res,
						       &out_max_transfer_size,
						       NULL,
						       error)) {
		fu_mm_mbim_device_error_convert(error);
		g_prefix_error_literal(error, "failed to parse file-open response: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send chunks */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       out_max_transfer_size);
	if (!fu_mm_qdu_mbim_device_write_chunks(self,
						chunks,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qdu_mbim_device_ensure_firmware_version(FuMmQduMbimDevice *self, GError **error)
{
	g_autofree gchar *firmware_version = NULL;
	g_autoptr(MbimMessage) request = NULL;
	g_autoptr(MbimMessage) response = NULL;

	request = mbim_message_device_caps_query_new(NULL);
	response =
	    fu_mm_mbim_device_command_sync(FU_MM_MBIM_DEVICE(self), request, 10 * 1000, error);
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
		g_debug("failed to parse caps-query response: ");
		return FALSE;
	}
	g_debug("modem query caps firmware version: %s", firmware_version);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qdu_mbim_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuMmQduMbimDevice *self = FU_MM_QDU_MBIM_DEVICE(device);
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
	if (!fu_mm_qdu_mbim_device_write(self, filename, data_part, progress, error))
		return FALSE;

	/* read back new version */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	if (!fu_mm_qdu_mbim_device_ensure_firmware_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qdu_mbim_device_probe(FuDevice *device, GError **error)
{
	FuMmQduMbimDevice *self = FU_MM_QDU_MBIM_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_MBIM, error);
}

static gboolean
fu_mm_qdu_mbim_device_prepare(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmQduMbimDevice *self = FU_MM_QDU_MBIM_DEVICE(device);
	if (!fu_mm_device_set_autosuspend_delay(FU_MM_DEVICE(self), 20000, error))
		return FALSE;
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_qdu_mbim_device_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmQduMbimDevice *self = FU_MM_QDU_MBIM_DEVICE(device);
	if (!fu_mm_device_set_autosuspend_delay(FU_MM_DEVICE(self), 2000, error))
		return FALSE;
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_qdu_mbim_device_set_progress(FuDevice *device, FuProgress *progress)
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
fu_mm_qdu_mbim_device_init(FuMmQduMbimDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.mbim_qdu");
}

static void
fu_mm_qdu_mbim_device_class_init(FuMmQduMbimDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->detach = fu_mm_qdu_mbim_device_detach;
	device_class->probe = fu_mm_qdu_mbim_device_probe;
	device_class->prepare = fu_mm_qdu_mbim_device_prepare;
	device_class->cleanup = fu_mm_qdu_mbim_device_cleanup;
	device_class->set_progress = fu_mm_qdu_mbim_device_set_progress;
	device_class->write_firmware = fu_mm_qdu_mbim_device_write_firmware;
}
