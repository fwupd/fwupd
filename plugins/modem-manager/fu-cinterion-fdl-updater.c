/*
 * Copyright 2024 TDT AG <development@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libmm-glib.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include "fu-cinterion-fdl-updater-struct.h"
#include "fu-cinterion-fdl-updater.h"

#ifdef HAVE_TERMIOS_H
#define FU_CINTERION_FDL_DEFAULT_BAUDRATE B115200
#endif
#define FU_CINTERION_FDL_MAX_READ_RETRIES  100
#define FU_CINTERION_FDL_MAX_WRITE_RETRIES 10
#define FU_CINTERION_FDL_SIZE_BYTES	   2

struct _FuCinterionFdlUpdater {
	GObject parent_instance;
	gchar *port;
	FuIOChannel *io_channel;
};

G_DEFINE_TYPE(FuCinterionFdlUpdater, fu_cinterion_fdl_updater, G_TYPE_OBJECT)

#if MM_CHECK_VERSION(1, 24, 0)
gboolean
fu_cinterion_fdl_updater_wait_ready(FuCinterionFdlUpdater *self, FuDevice *device, GError **error)
{
	guint8 byte = 0;
	gsize bytes_read = 0;

	for (guint i = 0; i < FU_CINTERION_FDL_MAX_READ_RETRIES; i++) {
		if (!fu_io_channel_read_raw(self->io_channel,
					    &byte,
					    0x1,
					    &bytes_read,
					    100,
					    FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					    error)) {
			return FALSE;
		}
		if (bytes_read == 1 && byte == FU_CINTERION_FDL_RESPONSE_OK) {
			g_debug("start signal read");
			return TRUE;
		}

		fu_device_sleep(device, 100);
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_READ,
		    "no response from device after %d reads",
		    FU_CINTERION_FDL_MAX_READ_RETRIES);
	return FALSE;
}

static gboolean
fu_cinterion_fdl_updater_set_io_flags(FuCinterionFdlUpdater *self, GError **error)
{
#ifdef HAVE_TERMIOS_H
	struct termios tio;
	gint fd;

	fd = fu_io_channel_unix_get_fd(self->io_channel);

	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = CS8 | CREAD | CLOCAL | HUPCL | FU_CINTERION_FDL_DEFAULT_BAUDRATE;

	if (tcsetattr(fd, TCSANOW, &tio) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "could not set termios attributes");
		return FALSE;
	}

	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <termios.h> not found");
	return FALSE;
#endif
}

gboolean
fu_cinterion_fdl_updater_open(FuCinterionFdlUpdater *self, GError **error)
{
	/* sanity check */
	if (self->port == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no port provided for update");
		return FALSE;
	}

	self->io_channel =
	    fu_io_channel_new_file(self->port,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (self->io_channel == NULL)
		return FALSE;

	if (!fu_cinterion_fdl_updater_set_io_flags(self, error))
		return FALSE;

	return TRUE;
}

gboolean
fu_cinterion_fdl_updater_close(FuCinterionFdlUpdater *self, GError **error)
{
	if (self->io_channel != NULL) {
		g_debug("closing io port...");
		if (!fu_io_channel_shutdown(self->io_channel, error))
			return FALSE;
		g_clear_object(&self->io_channel);
	}
	return TRUE;
}

static gboolean
fu_cinterion_fdl_updater_write_chunk(FuCinterionFdlUpdater *self,
				     GBytes *size_bytes,
				     GBytes *chunk_bytes,
				     GError **error)
{
	if (!fu_io_channel_write_bytes(self->io_channel,
				       size_bytes,
				       1500,
				       FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
				       error)) {
		return FALSE;
	}
	if (!fu_io_channel_write_bytes(self->io_channel,
				       chunk_bytes,
				       1500,
				       FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
				       error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_cinterion_fdl_updater_read_response(FuCinterionFdlUpdater *self,
				       FuDevice *device,
				       FuCinterionFdlResponse *response,
				       GError **error)
{
	guint8 byte = 0;
	gsize bytes_read = 0;

	for (guint i = 0; i < FU_CINTERION_FDL_MAX_READ_RETRIES; i++) {
		if (!fu_io_channel_read_raw(self->io_channel,
					    &byte,
					    0x1,
					    &bytes_read,
					    100,
					    FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					    error)) {
			return FALSE;
		}
		if (bytes_read != 1) {
			/* retry until byte read */
			fu_device_sleep(device, 10);
			continue;
		}

		switch (byte) {
		case FU_CINTERION_FDL_RESPONSE_OK:
			*response = FU_CINTERION_FDL_RESPONSE_OK;
			break;
		case FU_CINTERION_FDL_RESPONSE_RETRY:
			*response = FU_CINTERION_FDL_RESPONSE_RETRY;
			break;
		case FU_CINTERION_FDL_RESPONSE_BUSY:
			*response = FU_CINTERION_FDL_RESPONSE_BUSY;
			break;
		default:
			*response = FU_CINTERION_FDL_RESPONSE_UNKNOWN;
			break;
		}

		return TRUE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_READ,
		    "no response from device after %d reads",
		    FU_CINTERION_FDL_MAX_READ_RETRIES);
	return FALSE;
}

static gboolean
fu_cinterion_fdl_updater_write_chunk_retry(FuCinterionFdlUpdater *self,
					   FuDevice *device,
					   GBytes *size_bytes,
					   GBytes *chunk_bytes,
					   GError **error)
{
	guint write_retries = 0;

	while (write_retries < FU_CINTERION_FDL_MAX_WRITE_RETRIES) {
		guint read_retries = 0;
		FuCinterionFdlResponse response;

		if (!fu_cinterion_fdl_updater_write_chunk(self, size_bytes, chunk_bytes, error))
			return FALSE;

		while (read_retries < FU_CINTERION_FDL_MAX_READ_RETRIES) {
			if (!fu_cinterion_fdl_updater_read_response(self, device, &response, error))
				return FALSE;

			if (response == FU_CINTERION_FDL_RESPONSE_OK) {
				/* chunk written successfully, stop reading */
				return TRUE;
			}

			if (response == FU_CINTERION_FDL_RESPONSE_BUSY) {
				/* retry reading response */
				read_retries++;
			} else if (response == FU_CINTERION_FDL_RESPONSE_RETRY) {
				/* stop reading and retry write */
				break;
			} else {
				/* fatal response */
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INTERNAL,
						    "received fatal response");
				return FALSE;
			}
		}

		if (read_retries >= FU_CINTERION_FDL_MAX_READ_RETRIES) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "no response from device after %d reads",
				    FU_CINTERION_FDL_MAX_READ_RETRIES);
			return FALSE;
		}

		write_retries++;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "failed writing chunk %d times",
		    FU_CINTERION_FDL_MAX_WRITE_RETRIES);
	return FALSE;
}

gboolean
fu_cinterion_fdl_updater_write(FuCinterionFdlUpdater *self,
			       FuProgress *progress,
			       FuDevice *device,
			       GBytes *fw,
			       GError **error)
{
	guint chunk = 0;
	gsize offset = 0;
	gsize fw_len = g_bytes_get_size(fw);

	while (offset < fw_len) {
		g_autoptr(GBytes) size_bytes = NULL;
		g_autoptr(GBytes) chunk_bytes = NULL;
		guint16 chunk_size = 0;

		size_bytes = g_bytes_new_from_bytes(fw, offset, FU_CINTERION_FDL_SIZE_BYTES);
		if (!fu_memread_uint16_safe(g_bytes_get_data(size_bytes, NULL),
					    FU_CINTERION_FDL_SIZE_BYTES,
					    0x0,
					    &chunk_size,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		offset += FU_CINTERION_FDL_SIZE_BYTES;

		chunk_bytes = g_bytes_new_from_bytes(fw, offset, chunk_size);
		offset += chunk_size;

		if (!fu_cinterion_fdl_updater_write_chunk_retry(self,
								device,
								size_bytes,
								chunk_bytes,
								error)) {
			g_prefix_error(error, "could not write chunk %u", chunk);
			return FALSE;
		}
		if (chunk % 100 == 0)
			g_debug("wrote chunk %u successfully", chunk);

		fu_progress_set_percentage_full(progress, offset, fw_len);
		chunk++;
	}

	if (fw_len != offset) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "expected %" G_GSIZE_FORMAT " bytes, but wrote %" G_GSIZE_FORMAT,
			    fw_len,
			    offset);
		return FALSE;
	}

	return TRUE;
}
#endif // MM_CHECK_VERSION(1, 24, 0)

static void
fu_cinterion_fdl_updater_init(FuCinterionFdlUpdater *self)
{
}

static void
fu_cinterion_fdl_updater_finalize(GObject *object)
{
	FuCinterionFdlUpdater *self = FU_CINTERION_FDL_UPDATER(object);
	g_free(self->port);
	G_OBJECT_CLASS(fu_cinterion_fdl_updater_parent_class)->finalize(object);
}

static void
fu_cinterion_fdl_updater_class_init(FuCinterionFdlUpdaterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_cinterion_fdl_updater_finalize;
}

#if MM_CHECK_VERSION(1, 24, 0)
FuCinterionFdlUpdater *
fu_cinterion_fdl_updater_new(const gchar *port)
{
	FuCinterionFdlUpdater *self = g_object_new(FU_TYPE_CINTERION_FDL_UPDATER, NULL);
	self->port = g_strdup(port);
	return self;
}
#endif // MM_CHECK_VERSION(1, 24, 0)
