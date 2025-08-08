/*
 * Copyright 2024 TDT AG <development@tdt.de>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-dfota-device.h"

struct _FuMmDfotaDevice {
	FuMmDevice parent_instance;
};

G_DEFINE_TYPE(FuMmDfotaDevice, fu_mm_dfota_device, FU_TYPE_MM_DEVICE)

#define FU_MM_DFOTA_DEVICE_FILENAME "dfota_update.bin"

#define FU_MM_DFOTA_DEVICE_FOTA_READ_TIMEOUT_SECS 90
#define FU_MM_DFOTA_DEVICE_TIMEOUT_SECS		  5

static gboolean
fu_mm_dfota_device_probe(FuDevice *device, GError **error)
{
	FuMmDfotaDevice *self = FU_MM_DFOTA_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_AT, error);
}

static gboolean
fu_mm_dfota_device_setup(FuDevice *device, GError **error)
{
	FuMmDfotaDevice *self = FU_MM_DFOTA_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT+QFLST=?", TRUE, error)) {
		g_prefix_error(error, "listing files not supported: ");
		return FALSE;
	}
	/* if listing firmware file does not fail, there is an old firmware file to remove */
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self),
				 "AT+QFLST=\"UFS:" FU_MM_DFOTA_DEVICE_FILENAME "\"",
				 TRUE,
				 &error_local)) {
		g_debug("no old firmware found in filesystem: %s", error_local->message);
		return TRUE;
	}

	g_debug("found orphaned firmware file; trying to delete it");
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self),
				 "AT+QFDEL=\"" FU_MM_DFOTA_DEVICE_FILENAME "\"",
				 TRUE,
				 error)) {
		g_prefix_error(error, "failed to delete existing firmware file: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* compute 16 bit checksum based on bitwise XOR */
static gboolean
fu_mm_dfota_device_compute_checksum_cb(const guint8 *buf,
				       gsize bufsz,
				       gpointer user_data,
				       GError **error)
{
	guint16 *checksum = (guint16 *)user_data;
	for (gsize i = 0; i < bufsz; i += 2) {
		guint16 word = buf[i] << 8;
		if (i < bufsz - 1)
			word |= buf[i + 1];
		*checksum ^= word;
	}
	return TRUE;
}

static gboolean
fu_mm_dfota_device_upload_chunk(FuMmDfotaDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GBytes) ack_bytes = NULL;
	g_autoptr(GRegex) ack_regex = NULL;
	g_autoptr(GBytes) chunk_bytes = NULL;
	const gchar *ack_result = NULL;
	gsize ack_size;
	gsize chunk_size;
	gsize acks_expected;

	ack_regex = g_regex_new("^A+$", 0, 0, NULL);
	chunk_size = g_bytes_get_size(fu_chunk_get_bytes(chk));
	/* expect one byte as response for every 1024 bytes sent */
	acks_expected = chunk_size / 1024;
	/* pad every chunk to 2048 bytes to received correct amount of ACKs */
	chunk_bytes = fu_bytes_pad(fu_chunk_get_bytes(chk), 0x800, 0xFF);

	if (!fu_udev_device_write_bytes(FU_UDEV_DEVICE(self),
					chunk_bytes,
					1500,
					FU_IO_CHANNEL_FLAG_NONE,
					error)) {
		g_prefix_error(error, "failed to upload firmware to the device: ");
		return FALSE;
	}
	if (acks_expected == 0)
		return TRUE;

	ack_bytes = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
					      acks_expected,
					      FU_MM_DFOTA_DEVICE_TIMEOUT_SECS * 1000,
					      FU_IO_CHANNEL_FLAG_NONE,
					      error);
	if (ack_bytes == NULL) {
		g_prefix_error(error, "failed to read response: ");
		return FALSE;
	}

	ack_result = g_bytes_get_data(ack_bytes, &ack_size);
	if (ack_size != acks_expected) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "expected %" G_GSIZE_FORMAT " ACKs, got %" G_GSIZE_FORMAT,
			    acks_expected,
			    ack_size);
		return FALSE;
	}
	if (!g_regex_match(ack_regex, ack_result, 0, NULL)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "expected ACKs (A), got %s",
			    ack_result);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mm_dfota_device_parse_upload_result(FuMmDfotaDevice *self,
				       guint16 *checksum,
				       gsize *size,
				       GError **error)
{
	guint64 tmp;
	const gchar *result = NULL;
	g_autofree gchar *checksum_match = NULL;
	g_autofree gchar *size_match = NULL;
	g_autoptr(GBytes) result_bytes = NULL;
	g_autoptr(GMatchInfo) match_info = NULL;
	g_autoptr(GRegex) result_regex = NULL;

	/* +QFUPL: <filesize>,<hex checksum> */
	result_regex = g_regex_new("\\r\\n\\+QFUPL:\\s*(\\d+),([0-9a-f]+)\\r\\n", 0, 0, error);
	if (result_regex == NULL) {
		g_prefix_error(error, "failed to build regex: ");
		return FALSE;
	}

	result_bytes = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
						 4096,
						 FU_MM_DFOTA_DEVICE_TIMEOUT_SECS * 1000,
						 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						 error);
	if (result_bytes == NULL) {
		g_prefix_error(error, "failed to read AT+QFUPL response: ");
		return FALSE;
	}
	result = g_bytes_get_data(result_bytes, NULL);

	if (g_strrstr(result, "\r\nOK\r\n") == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "upload command exited with error");
		return FALSE;
	}
	if (!g_regex_match(result_regex, result, 0, &match_info)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "could not match QFUPL response");
		return FALSE;
	}
	if (!g_match_info_matches(match_info)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "could not match size and checksum");
		return FALSE;
	}

	/* success, so convert to integers */
	size_match = g_match_info_fetch(match_info, 1);
	checksum_match = g_match_info_fetch(match_info, 2);
	g_debug("parsed checksum '%s' and size '%s'", checksum_match, size_match);
	if (!fu_strtoull(size_match, &tmp, 0x0, G_MAXSIZE, FU_INTEGER_BASE_10, error))
		return FALSE;
	if (size != NULL)
		*size = tmp;
	if (!fu_strtoull(checksum_match, &tmp, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;
	if (checksum != NULL)
		*checksum = tmp;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_dfota_device_upload_stream(FuMmDfotaDevice *self, GInputStream *stream, GError **error)
{
	gsize size = 0;
	gsize size_parsed = 0;
	guint16 checksum = 0;
	guint16 checksum_parsed = 0;
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream, 0x0, FU_CHUNK_PAGESZ_NONE, 0x800, error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_mm_dfota_device_upload_chunk(self, chk, error)) {
			g_prefix_error(error, "failed at chunk %u: ", i);
			return FALSE;
		}
		if (i % 100 == 0)
			g_debug("wrote chunk %u/%u", i, fu_chunk_array_length(chunks) - 1);
	}

	/* check result */
	if (!fu_input_stream_size(stream, &size, error))
		return FALSE;
	if (!fu_input_stream_chunkify(stream,
				      fu_mm_dfota_device_compute_checksum_cb,
				      &checksum,
				      error))
		return FALSE;
	if (!fu_mm_dfota_device_parse_upload_result(self, &checksum_parsed, &size_parsed, error))
		return FALSE;
	if (size != size_parsed) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware size mismatch - expected 0x%x, but was 0x%x",
			    (guint)size,
			    (guint)size_parsed);
		return FALSE;
	}
	if (checksum != checksum_parsed) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "checksum mismatch - expected 0x%04x, but was 0x%04x",
			    checksum,
			    checksum_parsed);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_dfota_device_parse_fota_response(FuMmDfotaDevice *self,
				       const gchar *response,
				       FuProgress *progress,
				       gboolean *finished,
				       GError **error)
{
	g_autoptr(GRegex) fota_regex = NULL;
	g_autoptr(GMatchInfo) match_info = NULL;
	g_autofree gchar *status_match = NULL;
	g_autofree gchar *status_number_match = NULL;
	guint64 status_number = 0;

	/* +QIND: "FOTA","<STATUS>"(,<number>)? */
	fota_regex = g_regex_new("\\+QIND:\\s*\"FOTA\",\"([A-Z]+)\"(,(\\d+))?", 0, 0, error);
	if (fota_regex == NULL) {
		g_prefix_error(error, "failed to build regex: ");
		return FALSE;
	}

	if (!g_regex_match(fota_regex, response, 0, &match_info)) {
		/*
		 * Log and continue on unexpected responses because devices
		 * may incorrectly return an incomplete status message 1-2
		 * times.
		 */
		g_debug("got unexpected response '%s'", response);
		return TRUE;
	}
	if (!g_match_info_matches(match_info)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "could not match fota status");
		return FALSE;
	}

	status_match = g_match_info_fetch(match_info, 1);

	if (g_strcmp0(status_match, "START") == 0) {
		g_debug("update started successfully");
		return TRUE;
	}

	/* expect status and number, which means four matches in the above regex */
	if (g_match_info_get_match_count(match_info) != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "badly formatted message '%s'",
			    response);
		return FALSE;
	}

	status_number_match = g_match_info_fetch(match_info, 3);
	if (!g_ascii_string_to_unsigned(status_number_match,
					10,
					0,
					G_MAXUINT,
					&status_number,
					error))
		return FALSE;

	if (g_strcmp0(status_match, "UPDATING") == 0) {
		fu_progress_set_percentage(progress, (guint)status_number);
		return TRUE;
	}
	if (g_strcmp0(status_match, "END") == 0) {
		if (status_number != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "update exited with error code %" G_GUINT64_FORMAT,
				    status_number);
			return FALSE;
		}

		g_debug("updated finished successfully");
		*finished = TRUE;
		return TRUE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "unhandled fota status '%s'",
		    status_match);
	return FALSE;
}

static gboolean
fu_mm_dfota_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmDfotaDevice *self = FU_MM_DFOTA_DEVICE(device);
	gboolean finished = FALSE;

	while (!finished) {
		g_autoptr(GBytes) bytes = NULL;
		g_autofree gchar *result = NULL;

		bytes = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
						  4096,
						  FU_MM_DFOTA_DEVICE_FOTA_READ_TIMEOUT_SECS * 1000,
						  FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						  error);
		if (bytes == NULL)
			return FALSE;
		result = fu_strsafe_bytes(bytes, G_MAXSIZE);

		/* ignore empty responses */
		if (result == NULL)
			continue;

		g_strstrip(result);
		if (strlen(result) == 0)
			continue;

		if (!fu_mm_dfota_device_parse_fota_response(self,
							    result,
							    progress,
							    &finished,
							    error))
			return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_dfota_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuMmDfotaDevice *self = FU_MM_DFOTA_DEVICE(device);
	g_autofree gchar *upload_cmd = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* get default stream */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* put the device into upload mode */
	upload_cmd = g_strdup_printf("AT+QFUPL=\"%s\",%" G_GSIZE_FORMAT ",5,1",
				     FU_MM_DFOTA_DEVICE_FILENAME,
				     fu_firmware_get_size(firmware));
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), upload_cmd, TRUE, error)) {
		g_prefix_error(error, "failed to enable upload mode: ");
		return FALSE;
	}
	if (!fu_mm_dfota_device_upload_stream(self, stream, error))
		return FALSE;
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self),
				 "AT+QFOTADL=\"/data/ufs/" FU_MM_DFOTA_DEVICE_FILENAME "\"",
				 TRUE,
				 error)) {
		g_prefix_error(error, "failed to start update: ");
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_dfota_device_prepare(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuMmDfotaDevice *self = FU_MM_DFOTA_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_dfota_device_cleanup(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuMmDfotaDevice *self = FU_MM_DFOTA_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_dfota_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 13, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 85, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mm_dfota_device_init(FuMmDfotaDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.quectel.dfota");
	fu_device_set_remove_delay(FU_DEVICE(self), 15000);
}

static void
fu_mm_dfota_device_class_init(FuMmDfotaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_mm_dfota_device_probe;
	device_class->attach = fu_mm_dfota_device_attach;
	device_class->prepare = fu_mm_dfota_device_prepare;
	device_class->cleanup = fu_mm_dfota_device_cleanup;
	device_class->setup = fu_mm_dfota_device_setup;
	device_class->set_progress = fu_mm_dfota_device_set_progress;
	device_class->write_firmware = fu_mm_dfota_device_write_firmware;
}
