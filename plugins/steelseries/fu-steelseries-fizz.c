/*
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-device.h"
#include "fu-steelseries-firmware.h"
#include "fu-steelseries-fizz-impl.h"
#include "fu-steelseries-fizz-tunnel.h"
#include "fu-steelseries-fizz.h"

#define STEELSERIES_BUFFER_TRANSFER_SIZE 52

#define STEELSERIES_FIZZ_COMMAND_ERROR_SUCCESS		      0
#define STEELSERIES_FIZZ_COMMAND_ERROR_FILE_NOT_FOUND	      1
#define STEELSERIES_FIZZ_COMMAND_ERROR_FILE_TOO_SHORT	      2
#define STEELSERIES_FIZZ_COMMAND_ERROR_FLASH_FAILED	      3
#define STEELSERIES_FIZZ_COMMAND_ERROR_PERMISSION_DENIED      4
#define STEELSERIES_FIZZ_COMMAND_ERROR_OPERATION_NO_SUPPORTED 5

#define STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT 1U << 6

#define STEELSERIES_FIZZ_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_ERROR_OFFSET	0x01U

#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_COMMAND	     0x03U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_COMMAND_OFFSET    0x00U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_FILESYSTEM_OFFSET 0x01U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_ID_OFFSET	     0x02U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_SIZE_OFFSET	     0x03U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_OFFSET_OFFSET     0x05U
#define STEELSERIES_FIZZ_WRITE_ACCESS_FILE_DATA_OFFSET	     0x09U

#define STEELSERIES_FIZZ_READ_ACCESS_FILE_COMMAND	    0x83U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_COMMAND_OFFSET    0x00U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_FILESYSTEM_OFFSET 0x01U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_ID_OFFSET	    0x02U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_SIZE_OFFSET	    0x03U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_OFFSET_OFFSET	    0x05U
#define STEELSERIES_FIZZ_READ_ACCESS_FILE_DATA_OFFSET	    0x02U

#define STEELSERIES_FIZZ_ERASE_FILE_COMMAND	      0x02U
#define STEELSERIES_FIZZ_ERASE_FILE_COMMAND_OFFSET    0x0U
#define STEELSERIES_FIZZ_ERASE_FILE_FILESYSTEM_OFFSET 0x1U
#define STEELSERIES_FIZZ_ERASE_FILE_ID_OFFSET	      0x2U

#define STEELSERIES_FIZZ_RESET_COMMAND	      0x01U
#define STEELSERIES_FIZZ_RESET_COMMAND_OFFSET 0x0U
#define STEELSERIES_FIZZ_RESET_MODE_OFFSET    0x1U

#define STEELSERIES_FIZZ_FILE_CRC32_COMMAND		  0x84U
#define STEELSERIES_FIZZ_FILE_CRC32_COMMAND_OFFSET	  0x00U
#define STEELSERIES_FIZZ_FILE_CRC32_FILESYSTEM_OFFSET	  0x01U
#define STEELSERIES_FIZZ_FILE_CRC32_ID_OFFSET		  0x02U
#define STEELSERIES_FIZZ_FILE_CRC32_CALCULATED_CRC_OFFSET 0x02U
#define STEELSERIES_FIZZ_FILE_CRC32_STORED_CRC_OFFSET	  0x06U

struct _FuSteelseriesFizz {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizz, fu_steelseries_fizz, FU_TYPE_DEVICE)

static gboolean
fu_steelseries_fizz_command_error_to_error(guint8 cmd, guint8 err, GError **error)
{
	FwupdError err_code;

	switch (err) {
	/* success */
	case STEELSERIES_FIZZ_COMMAND_ERROR_SUCCESS:
		return TRUE;

	case STEELSERIES_FIZZ_COMMAND_ERROR_FILE_NOT_FOUND:
		err_code = FWUPD_ERROR_NOT_FOUND;
		break;

	/* targeted offset is past the file end */
	case STEELSERIES_FIZZ_COMMAND_ERROR_FILE_TOO_SHORT:
		err_code = FWUPD_ERROR_INVALID_DATA;
		break;

	/* when internal flash returns error */
	case STEELSERIES_FIZZ_COMMAND_ERROR_FLASH_FAILED:
		err_code = FWUPD_ERROR_INTERNAL;
		break;

	/* USB API doesn't have permission to access this file */
	case STEELSERIES_FIZZ_COMMAND_ERROR_PERMISSION_DENIED:
		err_code = FWUPD_ERROR_PERMISSION_DENIED;
		break;

	/* USB API doesn't have permission to access this file */
	case STEELSERIES_FIZZ_COMMAND_ERROR_OPERATION_NO_SUPPORTED:
		err_code = FWUPD_ERROR_NOT_SUPPORTED;
		break;

	/* fallback */
	default:
		err_code = FWUPD_ERROR_INTERNAL;
		break;
	}

	g_set_error(error, FWUPD_ERROR, err_code, "command 0x%02x returned error 0x%02x", cmd, err);
	return FALSE;
}

static gboolean
fu_steelseries_fizz_cmd(FuDevice *device,
			guint8 *data,
			gsize datasz,
			gboolean answer,
			GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_steelseries_fizz_impl_cmd(FU_STEELSERIES_FIZZ_IMPL(proxy),
					    data,
					    datasz,
					    answer,
					    error);
}

static gboolean
fu_steelseries_fizz_command_and_check_error(FuDevice *device,
					    guint8 *data,
					    gsize datasz,
					    GError **error)
{
	const guint8 command = data[0];
	guint8 err;
	guint8 cmd;

	if (!fu_steelseries_fizz_cmd(device, data, datasz, TRUE, error))
		return FALSE;

	if (!fu_memread_uint8_safe(data, datasz, STEELSERIES_FIZZ_COMMAND_OFFSET, &cmd, error))
		return FALSE;

	if (cmd != command) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "command invalid, got 0x%02x, expected 0x%02x",
			    cmd,
			    command);
		return FALSE;
	}

	if (!fu_memread_uint8_safe(data, datasz, STEELSERIES_FIZZ_ERROR_OFFSET, &err, error))
		return FALSE;

	return fu_steelseries_fizz_command_error_to_error(cmd, err, error);
}

static gboolean
fu_steelseries_fizz_write_fs(FuDevice *device,
			     gboolean tunnel,
			     guint8 fs,
			     guint8 id,
			     GBytes *fw,
			     FuProgress *progress,
			     GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_WRITE_ACCESS_FILE_COMMAND;
	g_autoptr(FuChunkArray) chunks = NULL;

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, STEELSERIES_BUFFER_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_memwrite_uint8_safe(data,
					    sizeof(data),
					    STEELSERIES_FIZZ_WRITE_ACCESS_FILE_COMMAND_OFFSET,
					    cmd,
					    error))
			return FALSE;

		if (!fu_memwrite_uint8_safe(data,
					    sizeof(data),
					    STEELSERIES_FIZZ_WRITE_ACCESS_FILE_FILESYSTEM_OFFSET,
					    fs,
					    error))
			return FALSE;

		if (!fu_memwrite_uint8_safe(data,
					    sizeof(data),
					    STEELSERIES_FIZZ_WRITE_ACCESS_FILE_ID_OFFSET,
					    id,
					    error))
			return FALSE;

		if (!fu_memwrite_uint16_safe(data,
					     sizeof(data),
					     STEELSERIES_FIZZ_WRITE_ACCESS_FILE_SIZE_OFFSET,
					     fu_chunk_get_data_sz(chk),
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		if (!fu_memwrite_uint32_safe(data,
					     sizeof(data),
					     STEELSERIES_FIZZ_WRITE_ACCESS_FILE_OFFSET_OFFSET,
					     fu_chunk_get_address(chk),
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

		fu_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));
		if (!fu_steelseries_fizz_command_and_check_error(device, data, sizeof(data), error))
			return FALSE;
		fu_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_erase_fs(FuDevice *device,
			     gboolean tunnel,
			     guint8 fs,
			     guint8 id,
			     GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_ERASE_FILE_COMMAND;

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_ERASE_FILE_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_ERASE_FILE_FILESYSTEM_OFFSET,
				    fs,
				    error))
		return FALSE;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_ERASE_FILE_ID_OFFSET,
				    id,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "EraseFile", data, sizeof(data));
	if (!fu_steelseries_fizz_command_and_check_error(device, data, sizeof(data), error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "EraseFile", data, sizeof(data));

	/* success */
	return TRUE;
}

gboolean
fu_steelseries_fizz_reset(FuDevice *device, gboolean tunnel, guint8 mode, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_RESET_COMMAND;

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_RESET_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_RESET_MODE_OFFSET,
				    mode,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "Reset", data, sizeof(data));
	if (!fu_steelseries_fizz_cmd(device, data, sizeof(data), FALSE, error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_steelseries_fizz_get_crc32_fs(FuDevice *device,
				 gboolean tunnel,
				 guint8 fs,
				 guint8 id,
				 guint32 *calculated_crc,
				 guint32 *stored_crc,
				 GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_FILE_CRC32_COMMAND;

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_FILE_CRC32_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_FILE_CRC32_FILESYSTEM_OFFSET,
				    fs,
				    error))
		return FALSE;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_FILE_CRC32_ID_OFFSET,
				    id,
				    error))

		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "FileCRC32", data, sizeof(data));
	if (!fu_steelseries_fizz_command_and_check_error(device, data, sizeof(data), error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "FileCRC32", data, sizeof(data));

	if (!fu_memread_uint32_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_FILE_CRC32_CALCULATED_CRC_OFFSET,
				    calculated_crc,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (!fu_memread_uint32_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_FILE_CRC32_STORED_CRC_OFFSET,
				    stored_crc,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_read_fs(FuDevice *device,
			    gboolean tunnel,
			    guint8 fs,
			    guint8 id,
			    guint8 *buf,
			    gsize bufsz,
			    FuProgress *progress,
			    GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_READ_ACCESS_FILE_COMMAND;
	g_autoptr(GPtrArray) chunks = NULL;

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0x0, 0x0, STEELSERIES_BUFFER_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		const guint16 size = fu_chunk_get_data_sz(chk);
		const guint32 offset = fu_chunk_get_address(chk);

		if (!fu_memwrite_uint8_safe(data,
					    sizeof(data),
					    STEELSERIES_FIZZ_READ_ACCESS_FILE_COMMAND_OFFSET,
					    cmd,
					    error))
			return FALSE;

		if (!fu_memwrite_uint8_safe(data,
					    sizeof(data),
					    STEELSERIES_FIZZ_READ_ACCESS_FILE_FILESYSTEM_OFFSET,
					    fs,
					    error))
			return FALSE;

		if (!fu_memwrite_uint8_safe(data,
					    sizeof(data),
					    STEELSERIES_FIZZ_READ_ACCESS_FILE_ID_OFFSET,
					    id,
					    error))
			return FALSE;

		if (!fu_memwrite_uint16_safe(data,
					     sizeof(data),
					     STEELSERIES_FIZZ_READ_ACCESS_FILE_SIZE_OFFSET,
					     size,
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		if (!fu_memwrite_uint32_safe(data,
					     sizeof(data),
					     STEELSERIES_FIZZ_READ_ACCESS_FILE_OFFSET_OFFSET,
					     offset,
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		fu_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));
		if (!fu_steelseries_fizz_command_and_check_error(device, data, sizeof(data), error))
			return FALSE;
		fu_dump_raw(G_LOG_DOMAIN, "AccessFile", data, sizeof(data));

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
fu_steelseries_fizz_get_paired_status(FuDevice *device, guint8 *status, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_steelseries_fizz_impl_get_paired_status(FU_STEELSERIES_FIZZ_IMPL(proxy),
							  status,
							  error);
}

gboolean
fu_steelseries_fizz_get_connection_status(FuDevice *device, guint8 *status, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_steelseries_fizz_impl_get_connection_status(FU_STEELSERIES_FIZZ_IMPL(proxy),
							      status,
							      error);
}

static gboolean
fu_steelseries_fizz_ensure_children(FuDevice *device, GError **error)
{
	guint8 status;
	g_autofree gchar *version = NULL;

	FuDevice *proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	/* not a USB receiver */
	if (!fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER))
		return TRUE;

	/* in bootloader mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_steelseries_fizz_get_paired_status(device, &status, error)) {
		g_prefix_error(error, "failed to get paired status: ");
		return FALSE;
	}

	if (status != 0) {
		g_autoptr(FuSteelseriesFizzTunnel) paired_device =
		    fu_steelseries_fizz_tunnel_new(FU_STEELSERIES_FIZZ(device));

		fu_device_set_proxy(FU_DEVICE(paired_device), FU_DEVICE(proxy));
		fu_device_add_child(device, FU_DEVICE(paired_device));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER) ||
	    !fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_DETACH_BOOTLOADER))
		return TRUE;

	/* switch to bootloader mode only if device needs it */
	if (!fu_steelseries_fizz_reset(device,
				       FALSE,
				       STEELSERIES_FIZZ_RESET_MODE_BOOTLOADER,
				       error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_steelseries_fizz_reset(device,
				       FALSE,
				       STEELSERIES_FIZZ_RESET_MODE_NORMAL,
				       &error_local))
		g_warning("failed to reset: %s", error_local->message);

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_setup(FuDevice *device, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autofree gchar *serial = NULL;
	g_autoptr(GError) error_local = NULL;

	FuDevice *proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	/* in bootloader mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		if (!fu_steelseries_fizz_ensure_children(device, error))
			return FALSE;
	}

	version =
	    fu_steelseries_fizz_impl_get_version(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);
	if (version == NULL) {
		g_prefix_error(error, "failed to get version: "); /* nocheck:set-version */
		return FALSE;
	}
	fu_device_set_version(device, version);

	if (!fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		/* direct connection */
		serial = fu_steelseries_fizz_impl_get_serial(FU_STEELSERIES_FIZZ_IMPL(proxy),
							     FALSE,
							     &error_local);
		if (serial != NULL) {
			fu_device_set_serial(device, serial);
			fu_device_set_equivalent_id(device, serial);
		}

		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	}
	/* success */
	return TRUE;
}

gboolean
fu_steelseries_fizz_write_firmware_fs(FuDevice *device,
				      gboolean tunnel,
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
	if (tunnel) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 13, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 87, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);
	} else {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 38, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 60, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);
	}

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	buf = fu_bytes_get_data_safe(blob, &bufsz, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_steelseries_fizz_erase_fs(device, tunnel, fs, id, error)) {
		g_prefix_error(error, "failed to erase FS 0x%02x ID 0x%02x: ", fs, id);
		return FALSE;
	}
	fu_progress_step_done(progress);
	if (!fu_steelseries_fizz_write_fs(device,
					  tunnel,
					  fs,
					  id,
					  blob,
					  fu_progress_get_child(progress),
					  error)) {
		g_prefix_error(error, "failed to write FS 0x%02x ID 0x%02x: ", fs, id);
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (!fu_steelseries_fizz_get_crc32_fs(device,
					      tunnel,
					      fs,
					      id,
					      &calculated_crc,
					      &stored_crc,
					      error)) {
		g_prefix_error(error, "failed to get CRC32 FS 0x%02x ID 0x%02x: ", fs, id);
		return FALSE;
	}
	if (calculated_crc != stored_crc) {
		g_warning("%s: checksum mismatch, got 0x%08x, expected 0x%08x",
			  fu_device_get_name(device),
			  calculated_crc,
			  stored_crc);
	}
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
	guint8 fs;
	guint8 id;
	gboolean is_receiver = FALSE;
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	is_receiver = fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER);
	fs =
	    fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy), is_receiver, error);
	id = fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy),
						  is_receiver,
						  error);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, 1);

	if (!fu_steelseries_fizz_write_firmware_fs(device,
						   FALSE,
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

FuFirmware *
fu_steelseries_fizz_read_firmware_fs(FuDevice *device,
				     gboolean tunnel,
				     guint8 fs,
				     guint8 id,
				     gsize size,
				     FuProgress *progress,
				     GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_steelseries_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 100, NULL);

	buf = g_malloc0(size);
	if (!fu_steelseries_fizz_read_fs(device,
					 tunnel,
					 fs,
					 id,
					 buf,
					 size,
					 fu_progress_get_child(progress),
					 error)) {
		g_prefix_error(error, "failed to read FS 0x%02x ID 0x%02x: ", fs, id);
		return NULL;
	}
	fu_progress_step_done(progress);

	fu_dump_raw(G_LOG_DOMAIN, "Firmware", buf, size);
	blob = g_bytes_new_take(g_steal_pointer(&buf), size);
	if (!fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static FuFirmware *
fu_steelseries_fizz_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 fs;
	guint8 id;
	gboolean is_receiver;
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return NULL;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return NULL;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 100, NULL);

	is_receiver = fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER);
	fs =
	    fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy), is_receiver, error);
	id = fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy),
						  is_receiver,
						  error);

	firmware = fu_steelseries_fizz_read_firmware_fs(device,
							FALSE,
							fs,
							id,
							fu_device_get_firmware_size_max(device),
							fu_progress_get_child(progress),
							error);
	if (firmware == NULL)
		return NULL;
	fu_progress_step_done(progress);

	/* success */
	return g_steal_pointer(&firmware);
}

static void
fu_steelseries_fizz_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 18, "reload");
}

static void
fu_steelseries_fizz_replace(FuDevice *device, FuDevice *donor)
{
	/* copy to device without serial, i.e. USB-C connection in bootloader mode */
	if (fu_device_get_equivalent_id(device) == NULL)
		fu_device_set_equivalent_id(device, fu_device_get_equivalent_id(donor));
}

static void
fu_steelseries_fizz_class_init(FuSteelseriesFizzClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_steelseries_fizz_detach;
	device_class->attach = fu_steelseries_fizz_attach;
	device_class->setup = fu_steelseries_fizz_setup;
	device_class->replace = fu_steelseries_fizz_replace;
	device_class->write_firmware = fu_steelseries_fizz_write_firmware;
	device_class->read_firmware = fu_steelseries_fizz_read_firmware;
	device_class->set_progress = fu_steelseries_fizz_set_progress;
}

static void
fu_steelseries_fizz_init(FuSteelseriesFizz *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_register_private_flag(FU_DEVICE(self), FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_STEELSERIES_DEVICE_FLAG_DETACH_BOOTLOADER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.fizz");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG); /* 40 s */
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_STEELSERIES_FIRMWARE);
	fu_device_set_priority(FU_DEVICE(self), 10); /* better than tunneled device */
}
