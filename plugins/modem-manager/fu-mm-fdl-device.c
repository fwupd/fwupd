/*
 * Copyright 2024 TDT AG <development@tdt.de>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-fdl-device.h"
#include "fu-mm-fdl-struct.h"

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef HAVE_TERMIOS_H
#define FU_CINTERION_FDL_DEFAULT_BAUDRATE B115200
#endif
#define FU_CINTERION_FDL_MAX_READ_RETRIES  100
#define FU_CINTERION_FDL_MAX_WRITE_RETRIES 10
#define FU_CINTERION_FDL_SIZE_BYTES	   2

struct _FuMmFdlDevice {
	FuMmDevice parent_instance;
};

G_DEFINE_TYPE(FuMmFdlDevice, fu_mm_fdl_device, FU_TYPE_MM_DEVICE)

static gboolean
fu_mm_fdl_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);

	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT", TRUE, error))
		return FALSE;
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT^SFDL", TRUE, error)) {
		g_prefix_error_literal(error, "enabling firmware download mode not supported: ");
		return FALSE;
	}

	/* wait 15 s before reopening port */
	fu_device_sleep(device, 15000);
	return TRUE;
}

static gboolean
fu_mm_fdl_device_wait_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	gsize bytes_read = 0;
	guint8 buf[1] = {0};

	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 buf,
				 sizeof(buf),
				 &bytes_read,
				 100,
				 FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
				 error)) {
		return FALSE;
	}
	if (bytes_read != 1 || buf[0] != FU_MM_CINTERION_FDL_RESPONSE_OK) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "invalid response");
		return FALSE;
	}

	/* success */
	g_debug("start signal read");
	return TRUE;
}

static gboolean
fu_mm_fdl_device_write_chunk(FuMmFdlDevice *self,
			     GBytes *size_bytes,
			     GBytes *chunk_bytes,
			     GError **error)
{
	if (!fu_udev_device_write_bytes(FU_UDEV_DEVICE(self),
					size_bytes,
					1500,
					FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					error)) {
		return FALSE;
	}
	if (!fu_udev_device_write_bytes(FU_UDEV_DEVICE(self),
					chunk_bytes,
					1500,
					FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_mm_fdl_device_read_response(FuMmFdlDevice *self,
			       FuMmCinterionFdlResponse *response,
			       GError **error)
{
	guint8 buf[1] = {0};
	gsize bytes_read = 0;

	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 buf,
				 sizeof(buf),
				 &bytes_read,
				 100,
				 FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
				 error)) {
		return FALSE;
	}
	if (bytes_read != 1) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "invalid response");
		return FALSE;
	}

	switch (buf[0]) {
	case FU_MM_CINTERION_FDL_RESPONSE_OK:
	case FU_MM_CINTERION_FDL_RESPONSE_RETRY:
	case FU_MM_CINTERION_FDL_RESPONSE_BUSY:
		*response = buf[0];
		break;
	default:
		*response = FU_MM_CINTERION_FDL_RESPONSE_UNKNOWN;
		break;
	}

	/* success */
	return TRUE;
}

typedef struct {
	GBytes *size_bytes;
	GBytes *chunk_bytes;
} FuMmFdlDeviceWriteHelper;

static void
fu_mm_fdl_device_write_helper_free(FuMmFdlDeviceWriteHelper *helper)
{
	if (helper->size_bytes)
		g_object_unref(helper->size_bytes);
	if (helper->chunk_bytes)
		g_object_unref(helper->chunk_bytes);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMmFdlDeviceWriteHelper, fu_mm_fdl_device_write_helper_free)

static gboolean
fu_mm_fdl_device_read_chunk_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	FuMmCinterionFdlResponse *response = (FuMmCinterionFdlResponse *)user_data;

	if (!fu_mm_fdl_device_read_response(self, response, error))
		return FALSE;

	/* retry reading response */
	if (*response == FU_MM_CINTERION_FDL_RESPONSE_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "response busy");
		return FALSE;
	}
	if (*response == FU_MM_CINTERION_FDL_RESPONSE_UNKNOWN) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "response unknown");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_fdl_device_write_chunk_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	FuMmFdlDeviceWriteHelper *helper = (FuMmFdlDeviceWriteHelper *)user_data;
	FuMmCinterionFdlResponse response = FU_MM_CINTERION_FDL_RESPONSE_UNKNOWN;

	if (!fu_mm_fdl_device_write_chunk(self, helper->size_bytes, helper->chunk_bytes, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_mm_fdl_device_read_chunk_cb,
				  FU_CINTERION_FDL_MAX_READ_RETRIES,
				  10, /* ms */
				  &response,
				  error))
		return FALSE;

	/* stop reading and retry write */
	if (response == FU_MM_CINTERION_FDL_RESPONSE_RETRY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "response retry");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_fdl_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	guint chunk = 0;
	gsize offset = 0;
	gsize fw_len = fu_firmware_get_size(firmware);
	g_autoptr(GBytes) fw = NULL;

	/* wait to be ready */
	if (!fu_device_retry_full(device,
				  fu_mm_fdl_device_wait_ready_cb,
				  FU_CINTERION_FDL_MAX_READ_RETRIES,
				  100, /* ms */
				  NULL,
				  error))
		return FALSE;

	/* send each [variable-sized] section */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	while (offset < fw_len) {
		g_autoptr(FuMmFdlDeviceWriteHelper) helper = g_new0(FuMmFdlDeviceWriteHelper, 1);
		g_autoptr(GBytes) size_bytes = NULL;
		g_autoptr(GBytes) chunk_bytes = NULL;
		guint16 chunk_size = 0;

		helper->size_bytes =
		    g_bytes_new_from_bytes(fw, offset, FU_CINTERION_FDL_SIZE_BYTES);
		if (!fu_memread_uint16_safe(g_bytes_get_data(helper->size_bytes, NULL),
					    g_bytes_get_size(helper->size_bytes),
					    0x0,
					    &chunk_size,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		offset += FU_CINTERION_FDL_SIZE_BYTES;

		helper->chunk_bytes = g_bytes_new_from_bytes(fw, offset, chunk_size);
		offset += chunk_size;

		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_mm_fdl_device_write_chunk_cb,
					  FU_CINTERION_FDL_MAX_WRITE_RETRIES,
					  10, /* ms */
					  helper,
					  error)) {
			g_prefix_error(error, "could not write chunk %u: ", chunk);
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

	/* success */
	return TRUE;
}

static gboolean
fu_mm_fdl_device_probe(FuDevice *device, GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_AT, error);
}

static gboolean
fu_mm_fdl_device_set_io_flags(FuMmFdlDevice *self, GError **error)
{
#ifdef HAVE_TERMIOS_H
	gint fd = fu_io_channel_unix_get_fd(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)));
	struct termios tio = {
	    .c_cflag = CS8 | CREAD | CLOCAL | HUPCL | FU_CINTERION_FDL_DEFAULT_BAUDRATE,
	};
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

static gboolean
fu_mm_fdl_device_open(FuDevice *device, GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_mm_fdl_device_parent_class)->open(device, error))
		return FALSE;
	return fu_mm_fdl_device_set_io_flags(self, error);
}

static gboolean
fu_mm_fdl_device_prepare(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_fdl_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuMmFdlDevice *self = FU_MM_FDL_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_fdl_device_set_progress(FuDevice *self, FuProgress *progress)
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
fu_mm_fdl_device_init(FuMmFdlDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_protocol(FU_DEVICE(self), "com.cinterion.fdl");
}

static void
fu_mm_fdl_device_class_init(FuMmFdlDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->open = fu_mm_fdl_device_open;
	device_class->probe = fu_mm_fdl_device_probe;
	device_class->detach = fu_mm_fdl_device_detach;
	device_class->prepare = fu_mm_fdl_device_prepare;
	device_class->cleanup = fu_mm_fdl_device_cleanup;
	device_class->set_progress = fu_mm_fdl_device_set_progress;
	device_class->write_firmware = fu_mm_fdl_device_write_firmware;
}
