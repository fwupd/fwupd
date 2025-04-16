/*
 * Copyright 2024 TDT AG <development@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libmm-glib.h>
#include <string.h>

#include "fu-dfota-updater.h"

#define FU_DFOTA_UPDATER_TIMEOUT_SECS		5
#define FU_DFOTA_UPDATER_FOTA_READ_TIMEOUT_SECS 90

struct _FuDfotaUpdater {
	GObject parent_instance;
	FuIOChannel *io_channel;
};

G_DEFINE_TYPE(FuDfotaUpdater, fu_dfota_updater, G_TYPE_OBJECT)

#if MM_CHECK_VERSION(1, 24, 0)
static gchar *
fu_dfota_updater_compute_checksum(GBytes *fw)
{
	/* compute 16 bit checksum based on bitwise XOR */
	guint16 checksum = 0;
	gsize size = 0;
	const guint8 *bytes = g_bytes_get_data(fw, &size);

	for (guint i = 0; i < size; i += 2) {
		guint16 word = bytes[i] << 8;

		if (i < size - 1)
			word |= bytes[i + 1];

		checksum ^= word;
	}

	return g_strdup_printf("%x", checksum);
}

static gboolean
fu_dfota_updater_upload_chunk(FuDfotaUpdater *self, FuChunk *chk, GError **error)
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

	if (!fu_io_channel_write_bytes(self->io_channel,
				       chunk_bytes,
				       1500,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error)) {
		g_prefix_error(error, "failed to upload firmware to the device: ");
		return FALSE;
	}
	if (acks_expected == 0)
		return TRUE;

	ack_bytes = fu_io_channel_read_bytes(self->io_channel,
					     acks_expected,
					     FU_DFOTA_UPDATER_TIMEOUT_SECS * 1000,
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
fu_dfota_updater_parse_upload_result(FuDfotaUpdater *self,
				     gchar **checksum,
				     gsize *size,
				     GError **error)
{
	guint64 val = 0;
	g_autoptr(GBytes) result_bytes = NULL;
	g_autoptr(GRegex) result_regex = NULL;
	g_autoptr(GMatchInfo) match_info = NULL;
	g_autofree gchar *size_match = NULL;
	g_autofree gchar *checksum_match = NULL;
	const gchar *result = NULL;

	/* +QFUPL: <filesize>,<hex checksum> */
	result_regex = g_regex_new("\\r\\n\\+QFUPL:\\s*(\\d+),([0-9a-f]+)\\r\\n", 0, 0, error);
	if (result_regex == NULL) {
		g_prefix_error(error, "failed to build regex: ");
		return FALSE;
	}

	result_bytes = fu_io_channel_read_bytes(self->io_channel,
						-1,
						FU_DFOTA_UPDATER_TIMEOUT_SECS * 1000,
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

	size_match = g_match_info_fetch(match_info, 1);
	checksum_match = g_match_info_fetch(match_info, 2);

	g_debug("parsed checksum '%s' and size '%s'", checksum_match, size_match);

	if (!g_ascii_string_to_unsigned(size_match, 10, 0, G_MAXSIZE, &val, error))
		return FALSE;
	*size = (gsize)val;

	*checksum = g_steal_pointer(&checksum_match);

	return TRUE;
}

gboolean
fu_dfota_updater_upload_firmware(FuDfotaUpdater *self, GBytes *fw, GError **error)
{
	g_autoptr(FuChunkArray) chunks = fu_chunk_array_new_from_bytes(fw,
								       FU_CHUNK_ADDR_OFFSET_NONE,
								       FU_CHUNK_PAGESZ_NONE,
								       0x800);
	guint chunk_count = fu_chunk_array_length(chunks);
	g_autofree gchar *checksum = fu_dfota_updater_compute_checksum(fw);
	g_autofree gchar *checksum_parsed = NULL;
	gsize size = g_bytes_get_size(fw);
	gsize size_parsed = 0;

	for (guint i = 0; i < chunk_count; i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);

		if (chk == NULL)
			return FALSE;

		if (!fu_dfota_updater_upload_chunk(self, chk, error)) {
			g_prefix_error(error, "failed at chunk %u: ", i);
			return FALSE;
		}
		if (i % 100 == 0)
			g_debug("wrote chunk %u/%u", i, chunk_count - 1);
	}

	if (!fu_dfota_updater_parse_upload_result(self, &checksum_parsed, &size_parsed, error))
		return FALSE;

	if (size != size_parsed) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware size mismatch - expected %" G_GSIZE_FORMAT
			    ", but was %" G_GSIZE_FORMAT,
			    size,
			    size_parsed);
		return FALSE;
	}
	if (g_strcmp0(checksum, checksum_parsed) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "checksum mismatch - expected %s, but was %s",
			    checksum,
			    checksum_parsed);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dfota_updater_open(FuDfotaUpdater *self, GError **error)
{
	if (self->io_channel == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no channel provided for update");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dfota_updater_close(FuDfotaUpdater *self, GError **error)
{
	/* closing will be handled by locker */
	return TRUE;
}

static gboolean
fu_dfota_updater_parse_fota_response(gchar *response,
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

gboolean
fu_dfota_updater_write(FuDfotaUpdater *self, FuProgress *progress, FuDevice *device, GError **error)
{
	gboolean finished = FALSE;

	while (!finished) {
		g_autoptr(GBytes) bytes = NULL;
		g_autofree gchar *result = NULL;

		bytes = fu_io_channel_read_bytes(self->io_channel,
						 -1,
						 FU_DFOTA_UPDATER_FOTA_READ_TIMEOUT_SECS * 1000,
						 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						 error);
		if (bytes == NULL)
			return FALSE;

		result = g_strdup(g_bytes_get_data(bytes, NULL));
		if (result == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "no data read from device");
			return FALSE;
		}
		g_strstrip(result);
		/* ignore empty responses */
		if (strlen(result) == 0)
			continue;

		if (!fu_dfota_updater_parse_fota_response(result, progress, &finished, error))
			return FALSE;
	}

	return TRUE;
}
#endif // MM_CHECK_VERSION(1, 24, 0)

static void
fu_dfota_updater_init(FuDfotaUpdater *self)
{
}

static void
fu_dfota_updater_finalize(GObject *object)
{
	FuDfotaUpdater *self = FU_DFOTA_UPDATER(object);
	if (self->io_channel != NULL)
		g_object_unref(self->io_channel);
	G_OBJECT_CLASS(fu_dfota_updater_parent_class)->finalize(object);
}

static void
fu_dfota_updater_class_init(FuDfotaUpdaterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_dfota_updater_finalize;
}

#if MM_CHECK_VERSION(1, 24, 0)
FuDfotaUpdater *
fu_dfota_updater_new(FuIOChannel *io_channel)
{
	FuDfotaUpdater *self = g_object_new(FU_TYPE_DFOTA_UPDATER, NULL);
	self->io_channel = g_object_ref(io_channel);
	return self;
}
#endif // MM_CHECK_VERSION(1, 24, 0)
