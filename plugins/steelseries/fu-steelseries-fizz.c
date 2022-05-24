/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-firmware.h"
#include "fu-steelseries-fizz.h"

#define STEELSERIES_BUFFER_TRANSFER_SIZE 52

#define STEELSERIES_FIZZ_FILESYSTEM_RECEIVER 0x01U
#define STEELSERIES_FIZZ_FILESYSTEM_MOUSE    0x02U

#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_MAIN_BOOT_ID	  0x01U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FSDATA_FILE_ID	  0x02U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FACTORY_SETTINGS_ID  0x03U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_MAIN_APP_ID	  0x04U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID	  0x05U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_MOUSE_ID	  0x06U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_LIGHTING_ID 0x0fU
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_DEVICE_ID	  0x10U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_RESERVED_ID 0x11U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_RECOVERY_ID	  0x0dU
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FREE_SPACE_ID	  0xf1U

#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_SOFT_DEVICE_ID	0x00U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_MOUSE_ID	0x06U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MAIN_APP_ID		0x07U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID		0x08U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MSB_DATA_ID		0x09U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FACTORY_SETTINGS_ID	0x0aU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FSDATA_FILE_ID	0x0bU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MAIN_BOOT_ID		0x0cU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_RECOVERY_ID		0x0eU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_LIGHTING_ID	0x0fU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_DEVICE_ID	0x10U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FDS_PAGES_ID		0x12U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_BLUETOOTH_ID 0x13U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FREE_SPACE_ID		0xf0U

#define STEELSERIES_FIZZ_COMMAND_ERROR_SUCCESS		      0
#define STEELSERIES_FIZZ_COMMAND_ERROR_FILE_NOT_FOUND	      1
#define STEELSERIES_FIZZ_COMMAND_ERROR_FILE_TOO_SHORT	      2
#define STEELSERIES_FIZZ_COMMAND_ERROR_FLASH_FAILED	      3
#define STEELSERIES_FIZZ_COMMAND_ERROR_PERMISSION_DENIED      4
#define STEELSERIES_FIZZ_COMMAND_ERROR_OPERATION_NO_SUPPORTED 5

#define STEELSERIES_FIZZ_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_ERROR_OFFSET	0x01U

#define STEELSERIES_FIZZ_VERSION_COMMAND	0x90U
#define STEELSERIES_FIZZ_VERSION_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_VERSION_MODE_OFFSET	0x01U

#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_COMMAND_OFFSET    0x00U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_FILESYSTEM_OFFSET 0x01U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_ID_OFFSET	     0x02U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_SIZE_OFFSET	     0x03U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_OFFSET_OFFSET     0x05U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_DATA_OFFSET	     0x09U

#define STEELSERIES_FIZZ_READ_ACCESS_FILE_COMMAND_OFFSET    0x00U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_FILESYSTEM_OFFSET 0x01U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_ID_OFFSET	    0x02U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_SIZE_OFFSET	    0x03U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_OFFSET_OFFSET	    0x05U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_DATA_OFFSET	    0x02U

#define STEELSERIES_FIZZ_ERASE_FILE_COMMAND_OFFSET    0x0U
#define STEELSERIES_FIZZ_ERASE_FILE_FILESYSTEM_OFFSET 0x1U
#define STEELSERIES_FIZZ_ERASE_FILE_ID_OFFSET	      0x2U

#define STEELSERIES_FIZZ_RESET_COMMAND_OFFSET 0x0U
#define STEELSERIES_FIZZ_RESET_MODE_OFFSET    0x1U

struct _FuSteelseriesFizz {
	FuSteelseriesDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizz, fu_steelseries_fizz, FU_TYPE_STEELSERIES_DEVICE)

static gboolean
fu_steelseries_device_command_error_to_error(guint8 cmd, guint8 err, GError **error)
{
	/* success */
	if (err == STEELSERIES_FIZZ_COMMAND_ERROR_SUCCESS)
		return TRUE;

	if (err == STEELSERIES_FIZZ_COMMAND_ERROR_FILE_NOT_FOUND) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_FILE_ERROR_NOENT,
			    "command 0x%02x returned error 0x%02x",
			    cmd,
			    err);
		return FALSE;
	}

	/* targeted offset is past the file end */
	if (err == STEELSERIES_FIZZ_COMMAND_ERROR_FILE_TOO_SHORT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_FILE_ERROR_NOSPC,
			    "command 0x%02x returned error 0x%02x",
			    cmd,
			    err);
		return FALSE;
	}

	/* when internal flash returns error */
	if (err == STEELSERIES_FIZZ_COMMAND_ERROR_FLASH_FAILED) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_FILE_ERROR_IO,
			    "command 0x%02x returned error 0x%02x",
			    cmd,
			    err);
		return FALSE;
	}

	/* USB API doesn't have permission to access this file */
	if (err == STEELSERIES_FIZZ_COMMAND_ERROR_PERMISSION_DENIED) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_FILE_ERROR_ACCES,
			    "command 0x%02x returned error 0x%02x",
			    cmd,
			    err);
		return FALSE;
	}

	/* USB API doesn't have permission to access this file */
	if (err == STEELSERIES_FIZZ_COMMAND_ERROR_OPERATION_NO_SUPPORTED) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_FILE_ERROR_PERM,
			    "command 0x%02x returned error 0x%02x",
			    cmd,
			    err);
		return FALSE;
	}

	/* fallback */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "command 0x%02x returned error 0x%02x",
		    cmd,
		    err);
	return FALSE;
}

static gboolean
fu_steelseries_device_command_and_check_error(FuDevice *device, guint8 *data, GError **error)
{
	gint gerr = G_FILE_ERROR_FAILED;
	const guint8 command = data[0];
	guint8 err;
	guint8 cmd;
	gsize transfer_sz = fu_steelseries_device_get_transfer_size(FU_STEELSERIES_DEVICE(device));

	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, TRUE, error))
		return FALSE;

	if (!fu_common_read_uint8_safe(data,
				       transfer_sz,
				       STEELSERIES_FIZZ_COMMAND_OFFSET,
				       &cmd,
				       error))
		return FALSE;

	if (cmd != command) {
		g_set_error(error,
			    G_IO_ERROR,
			    gerr,
			    "command invalid, got 0x%02x, expected 0x%02x",
			    cmd,
			    command);
		return FALSE;
	}

	if (!fu_common_read_uint8_safe(data,
				       transfer_sz,
				       STEELSERIES_FIZZ_ERROR_OFFSET,
				       &err,
				       error))
		return FALSE;

	return fu_steelseries_device_command_error_to_error(cmd, err, error);
}

static gchar *
fu_steelseries_fizz_version(FuDevice *device, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	const guint16 cmd = 0x90U;
	const guint8 mode = 0U; /* string */

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_VERSION_COMMAND_OFFSET,
					cmd,
					error))
		return NULL;

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_VERSION_MODE_OFFSET,
					mode,
					error))
		return NULL;

	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));
	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, TRUE, error))
		return NULL;
	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));

	/* success */
	return g_strndup((const gchar *)data, sizeof(data));
}

static gboolean
fu_steelseries_fizz_write_access_file(FuDevice *device,
				      guint8 fs,
				      guint8 id,
				      const guint8 *buf,
				      gsize bufsz,
				      FuProgress *progress,
				      GError **error)
{
	const guint16 cmd = 0x03U;
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, STEELSERIES_BUFFER_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		const guint16 size = fu_chunk_get_data_sz(chk);
		const guint32 offset = fu_chunk_get_address(chk);

		if (!fu_common_write_uint8_safe(data,
						sizeof(data),
						STEELSERIES_FIZZ_WRITE_ACCESS_FILE_COMMAND_OFFSET,
						cmd,
						error))
			return FALSE;

		if (!fu_common_write_uint8_safe(
			data,
			sizeof(data),
			STEELSERIES_FIZZ_WRITE_ACCESS_FILE_FILESYSTEM_OFFSET,
			fs,
			error))
			return FALSE;

		if (!fu_common_write_uint8_safe(data,
						sizeof(data),
						STEELSERIES_FIZZ_WRITE_ACCESS_FILE_ID_OFFSET,
						id,
						error))
			return FALSE;

		if (!fu_common_write_uint16_safe(data,
						 sizeof(data),
						 STEELSERIES_FIZZ_WRITE_ACCESS_FILE_SIZE_OFFSET,
						 size,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;

		if (!fu_common_write_uint32_safe(data,
						 sizeof(data),
						 STEELSERIES_FIZZ_WRITE_ACCESS_FILE_OFFSET_OFFSET,
						 offset,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;

		if (!fu_memcpy_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_WRITE_ACCESS_FILE_DATA_OFFSET,
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
			fu_common_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));
		if (!fu_steelseries_device_command_and_check_error(device, data, error))
			return FALSE;
		if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
			fu_common_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_erase_file(FuDevice *device, guint8 fs, guint8 id, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	const guint16 cmd = 0x02U;

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_ERASE_FILE_COMMAND_OFFSET,
					cmd,
					error))
		return FALSE;

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_ERASE_FILE_FILESYSTEM_OFFSET,
					fs,
					error))
		return FALSE;

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_ERASE_FILE_ID_OFFSET,
					id,
					error))
		return FALSE;

	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "EraseFile", data, sizeof(data));
	if (!fu_steelseries_device_command_and_check_error(device, data, error))
		return FALSE;
	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "EraseFile", data, sizeof(data));

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_reset(FuDevice *device, guint8 mode, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	const guint16 cmd = 0x01U;

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_RESET_COMMAND_OFFSET,
					cmd,
					error))
		return FALSE;

	if (!fu_common_write_uint8_safe(data,
					sizeof(data),
					STEELSERIES_FIZZ_RESET_MODE_OFFSET,
					mode,
					error))
		return FALSE;

	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "Reset", data, sizeof(data));
	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, FALSE, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_file_crc32(FuDevice *device,
			       guint8 fs,
			       guint8 id,
			       guint32 *calculated_crc,
			       guint32 *stored_crc,
			       GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	const guint16 cmd = 0x84U;

	if (!fu_common_write_uint8_safe(data, sizeof(data), 0x00U, cmd, error))
		return FALSE;

	if (!fu_common_write_uint8_safe(data, sizeof(data), 0x01U, fs, error))
		return FALSE;

	if (!fu_common_write_uint8_safe(data, sizeof(data), 0x02U, id, error))
		return FALSE;

	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "FileCRC32", data, sizeof(data));
	if (!fu_steelseries_device_command_and_check_error(device, data, error))
		return FALSE;
	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "FileCRC32", data, sizeof(data));

	if (!fu_common_read_uint32_safe(data,
					sizeof(data),
					0x02U,
					calculated_crc,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	if (!fu_common_read_uint32_safe(data,
					sizeof(data),
					0x06U,
					stored_crc,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_read_access_file(FuDevice *device,
				     guint8 fs,
				     guint8 id,
				     guint8 *buf,
				     gsize bufsz,
				     FuProgress *progress,
				     GError **error)
{
	const guint16 cmd = 0x83U;
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0x0, 0x0, STEELSERIES_BUFFER_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		const guint16 size = fu_chunk_get_data_sz(chk);
		const guint32 offset = fu_chunk_get_address(chk);

		if (!fu_common_write_uint8_safe(data,
						sizeof(data),
						STEELSERIES_FIZZ_READ_ACCESS_FILE_COMMAND_OFFSET,
						cmd,
						error))
			return FALSE;

		if (!fu_common_write_uint8_safe(data,
						sizeof(data),
						STEELSERIES_FIZZ_READ_ACCESS_FILE_FILESYSTEM_OFFSET,
						fs,
						error))
			return FALSE;

		if (!fu_common_write_uint8_safe(data,
						sizeof(data),
						STEELSERIES_FIZZ_READ_ACCESS_FILE_ID_OFFSET,
						id,
						error))
			return FALSE;

		if (!fu_common_write_uint16_safe(data,
						 sizeof(data),
						 STEELSERIES_FIZZ_READ_ACCESS_FILE_SIZE_OFFSET,
						 size,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;

		if (!fu_common_write_uint32_safe(data,
						 sizeof(data),
						 STEELSERIES_FIZZ_READ_ACCESS_FILE_OFFSET_OFFSET,
						 offset,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;

		if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
			fu_common_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));
		if (!fu_steelseries_device_command_and_check_error(device, data, error))
			return FALSE;
		if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
			fu_common_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));

		if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
				    fu_chunk_get_data_sz(chk),
				    0x00,
				    data,
				    sizeof(data),
				    STEELSERIES_FIZZ_READ_ACCESS_FILE_DATA_OFFSET,
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	guint8 mode = 0x00U; /* normal */

	if (!fu_steelseries_fizz_reset(device, mode, &error_local))
		g_warning("failed to reset: %s", error_local->message);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_setup(FuDevice *device, GError **error)
{
	guint32 calculated_crc;
	guint32 stored_crc;
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_steelseries_fizz_parent_class)->setup(device, error))
		return FALSE;

	version = fu_steelseries_fizz_version(device, error);
	if (version == NULL) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}
	fu_device_set_version(device, version);

	/* it is a USB receiver */
	if (fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		fs = STEELSERIES_FIZZ_FILESYSTEM_RECEIVER;
		id = STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID;
	}

	if (!fu_steelseries_fizz_file_crc32(device, fs, id, &calculated_crc, &stored_crc, error)) {
		g_prefix_error(error,
			       "failed to get file CRC32 from FS 0x%02x ID 0x%02x: ",
			       fs,
			       id);
		return FALSE;
	}

	if (calculated_crc != stored_crc)
		g_warning("%s: checksum mismatch, got 0x%08x, expected 0x%08x",
			  fu_device_get_name(device),
			  calculated_crc,
			  stored_crc);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_write_file(FuDevice *device,
			       guint8 fs,
			       guint8 id,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	guint32 calculated_crc;
	guint32 stored_crc;
	const guint8 *buf;
	gsize bufsz;
	g_autoptr(GBytes) blob = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 38);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 60);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2);

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	buf = fu_bytes_get_data_safe(blob, &bufsz, error);
	if (buf == NULL)
		return FALSE;
	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "File", buf, bufsz);
	if (!fu_steelseries_fizz_erase_file(device, fs, id, error)) {
		g_prefix_error(error, "failed to erase file 0x%02x:0x%02x: ", fs, id);
		return FALSE;
	}
	fu_progress_step_done(progress);
	if (!fu_steelseries_fizz_write_access_file(device,
						   fs,
						   id,
						   buf,
						   bufsz,
						   fu_progress_get_child(progress),
						   error)) {
		g_prefix_error(error, "failed to write file 0x%02x:0x%02x: ", fs, id);
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (!fu_steelseries_fizz_file_crc32(device, fs, id, &calculated_crc, &stored_crc, error)) {
		g_prefix_error(error,
			       "failed to get file CRC32 from FS 0x%02x ID 0x%02x: ",
			       fs,
			       id);
		return FALSE;
	}
	if (calculated_crc != stored_crc)
		g_warning("%s: checksum mismatch, got 0x%08x, expected 0x%08x",
			  fu_device_get_name(device),
			  calculated_crc,
			  stored_crc);
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;

	/* it is a USB receiver */
	if (fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		fs = STEELSERIES_FIZZ_FILESYSTEM_RECEIVER;
		id = STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100);

	if (!fu_steelseries_fizz_write_file(device,
					    fs,
					    id,
					    firmware,
					    fu_progress_get_child(progress),
					    flags,
					    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_steelseries_fizz_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;
	gsize bufsz = 0x27000;
	g_autoptr(FuFirmware) firmware = fu_steelseries_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 100);

	/* it is a USB receiver */
	if (fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		fs = STEELSERIES_FIZZ_FILESYSTEM_RECEIVER;
		id = STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID;
		bufsz = 0x23000;
	}

	buf = g_malloc0(bufsz);
	if (!fu_steelseries_fizz_read_access_file(device,
						  fs,
						  id,
						  buf,
						  bufsz,
						  fu_progress_get_child(progress),
						  error))
		return NULL;
	fu_progress_step_done(progress);

	if (g_getenv("FWUPD_STEELSERIES_FIZZ_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "Firmware", buf, bufsz);
	blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	if (!fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static FuFirmware *
fu_steelseries_fizz_prepare_firmware(FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_steelseries_firmware_new();

	if (!fu_firmware_parse(FU_FIRMWARE(firmware), fw, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static void
fu_steelseries_fizz_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 82);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 18);	/* reload */
}

static void
fu_steelseries_fizz_class_init(FuSteelseriesFizzClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->attach = fu_steelseries_fizz_attach;
	klass_device->setup = fu_steelseries_fizz_setup;
	klass_device->write_firmware = fu_steelseries_fizz_write_firmware;
	klass_device->read_firmware = fu_steelseries_fizz_read_firmware;
	klass_device->prepare_firmware = fu_steelseries_fizz_prepare_firmware;
	klass_device->set_progress = fu_steelseries_fizz_set_progress;
}

static void
fu_steelseries_fizz_init(FuSteelseriesFizz *self)
{
	fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), 0x03);

	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.fizz");
	fu_device_set_install_duration(FU_DEVICE(self), 13); /* 13 s */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
}
