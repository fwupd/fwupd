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

#define FU_STEELSERIES_BUFFER_TRANSFER_SIZE 52

struct _FuSteelseriesFizz {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizz, fu_steelseries_fizz, FU_TYPE_USB_DEVICE)

static gboolean
fu_steelseries_fizz_command_error_to_error(guint8 cmd, guint8 err, GError **error)
{
	FwupdError err_code;

	switch (err) {
	/* success */
	case FU_STEELSERIES_FIZZ_COMMAND_ERROR_SUCCESS:
		return TRUE;

	case FU_STEELSERIES_FIZZ_COMMAND_ERROR_FILE_NOT_FOUND:
		err_code = FWUPD_ERROR_NOT_FOUND;
		break;

	/* targeted offset is past the file end */
	case FU_STEELSERIES_FIZZ_COMMAND_ERROR_FILE_TOO_SHORT:
		err_code = FWUPD_ERROR_INVALID_DATA;
		break;

	/* when internal flash returns error */
	case FU_STEELSERIES_FIZZ_COMMAND_ERROR_FLASH_FAILED:
		err_code = FWUPD_ERROR_INTERNAL;
		break;

	/* USB API doesn't have permission to access this file */
	case FU_STEELSERIES_FIZZ_COMMAND_ERROR_PERMISSION_DENIED:
		err_code = FWUPD_ERROR_PERMISSION_DENIED;
		break;

	/* USB API doesn't have permission to access this file */
	case FU_STEELSERIES_FIZZ_COMMAND_ERROR_OPERATION_NO_SUPPORTED:
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
fu_steelseries_fizz_request(FuSteelseriesFizz *self, GByteArray *buf, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN,
		    fu_steelseries_fizz_cmd_to_string(buf->data[0]),
		    buf->data,
		    buf->len);
	return fu_steelseries_fizz_impl_request(FU_STEELSERIES_FIZZ_IMPL(proxy), buf, error);
}

static GByteArray *
fu_steelseries_fizz_response(FuSteelseriesFizz *self, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return NULL;
	}
	return fu_steelseries_fizz_impl_response(FU_STEELSERIES_FIZZ_IMPL(proxy), error);
}

static GByteArray *
fu_steelseries_fizz_request_response(FuSteelseriesFizz *self, GByteArray *buf, GError **error)
{
	const FuSteelseriesFizzCmd cmd = buf->data[0];
	g_autoptr(FuStructSteelseriesFizzGenericRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_fizz_request(self, buf, error))
		return NULL;
	buf_res = fu_steelseries_fizz_response(self, error);
	if (buf_res == NULL)
		return NULL;
	st_res =
	    fu_struct_steelseries_fizz_generic_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (fu_struct_steelseries_fizz_generic_res_get_cmd(st_res) != cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "command invalid, got 0x%02x, expected 0x%02x",
			    fu_struct_steelseries_fizz_generic_res_get_cmd(st_res),
			    cmd);
		return NULL;
	}
	if (!fu_steelseries_fizz_command_error_to_error(
		cmd,
		fu_struct_steelseries_fizz_generic_res_get_error(st_res),
		error))
		return NULL;

	return g_steal_pointer(&buf_res);
}

static gboolean
fu_steelseries_fizz_write_fs(FuSteelseriesFizz *self,
			     gboolean tunnel,
			     guint8 fs,
			     guint8 id,
			     GBytes *fw,
			     FuProgress *progress,
			     GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_WRITE_ACCESS_FILE;
	g_autoptr(FuChunkArray) chunks = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_STEELSERIES_BUFFER_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructSteelseriesFizzWriteAccessFileReq) st_req =
		    fu_struct_steelseries_fizz_write_access_file_req_new();
		g_autoptr(GByteArray) buf_res = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		fu_struct_steelseries_fizz_write_access_file_req_set_cmd(st_req, cmd);
		fu_struct_steelseries_fizz_write_access_file_req_set_filesystem(st_req, fs);
		fu_struct_steelseries_fizz_write_access_file_req_set_id(st_req, id);
		fu_struct_steelseries_fizz_write_access_file_req_set_size(
		    st_req,
		    fu_chunk_get_data_sz(chk));
		fu_struct_steelseries_fizz_write_access_file_req_set_offset(
		    st_req,
		    fu_chunk_get_address(chk));
		if (!fu_struct_steelseries_fizz_write_access_file_req_set_data(
			st_req,
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			error))
			return FALSE;
		buf_res = fu_steelseries_fizz_request_response(self, st_req, error);
		if (buf_res == NULL)
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_erase_fs(FuSteelseriesFizz *self,
			     gboolean tunnel,
			     guint8 fs,
			     guint8 id,
			     GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_ERASE_FILE;
	g_autoptr(FuStructSteelseriesFizzEraseFileReq) st_req = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	st_req = fu_struct_steelseries_fizz_erase_file_req_new();
	fu_struct_steelseries_fizz_erase_file_req_set_cmd(st_req, cmd);
	fu_struct_steelseries_fizz_erase_file_req_set_filesystem(st_req, fs);
	fu_struct_steelseries_fizz_erase_file_req_set_id(st_req, id);
	buf_res = fu_steelseries_fizz_request_response(self, st_req, error);
	return buf_res != NULL;
}

gboolean
fu_steelseries_fizz_reset(FuSteelseriesFizz *self,
			  gboolean tunnel,
			  FuSteelseriesFizzResetMode mode,
			  GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_RESET;
	g_autoptr(FuStructSteelseriesFizzResetReq) st_req = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	st_req = fu_struct_steelseries_fizz_reset_req_new();
	fu_struct_steelseries_fizz_reset_req_set_cmd(st_req, cmd);
	fu_struct_steelseries_fizz_reset_req_set_mode(st_req, mode);
	return fu_steelseries_fizz_request(self, st_req, error);
}

gboolean
fu_steelseries_fizz_get_crc32_fs(FuSteelseriesFizz *self,
				 gboolean tunnel,
				 guint8 fs,
				 guint8 id,
				 guint32 *calculated_crc,
				 guint32 *stored_crc,
				 GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_FILE_CRC32;
	g_autoptr(FuStructSteelseriesFizzFileCrc32Req) st_req = NULL;
	g_autoptr(FuStructSteelseriesFizzFileCrc32Res) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	st_req = fu_struct_steelseries_fizz_file_crc32_req_new();
	fu_struct_steelseries_fizz_file_crc32_req_set_cmd(st_req, cmd);
	fu_struct_steelseries_fizz_file_crc32_req_set_filesystem(st_req, fs);
	fu_struct_steelseries_fizz_file_crc32_req_set_id(st_req, id);
	buf_res = fu_steelseries_fizz_request_response(self, st_req, error);
	if (buf_res == NULL)
		return FALSE;
	st_res = fu_struct_steelseries_fizz_file_crc32_res_parse(buf_res->data,
								 buf_res->len,
								 0x0,
								 error);
	if (st_res == NULL)
		return FALSE;
	*calculated_crc = fu_struct_steelseries_fizz_file_crc32_res_get_calculated(st_res);
	*stored_crc = fu_struct_steelseries_fizz_file_crc32_res_get_stored(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_read_fs(FuSteelseriesFizz *self,
			    gboolean tunnel,
			    guint8 fs,
			    guint8 id,
			    guint8 *buf,
			    gsize bufsz,
			    FuProgress *progress,
			    GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_READ_ACCESS_FILE;
	g_autoptr(GPtrArray) chunks = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	chunks =
	    fu_chunk_array_mutable_new(buf, bufsz, 0x0, 0x0, FU_STEELSERIES_BUFFER_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		const guint8 *data;
		gsize datasz = 0;
		g_autoptr(FuStructSteelseriesFizzReadAccessFileReq) st_req = NULL;
		g_autoptr(FuStructSteelseriesFizzReadAccessFileRes) st_res = NULL;
		g_autoptr(GByteArray) buf_res = NULL;

		st_req = fu_struct_steelseries_fizz_read_access_file_req_new();
		fu_struct_steelseries_fizz_read_access_file_req_set_cmd(st_req, cmd);
		fu_struct_steelseries_fizz_read_access_file_req_set_filesystem(st_req, fs);
		fu_struct_steelseries_fizz_read_access_file_req_set_id(st_req, id);
		fu_struct_steelseries_fizz_read_access_file_req_set_size(st_req,
									 fu_chunk_get_data_sz(chk));
		fu_struct_steelseries_fizz_read_access_file_req_set_offset(
		    st_req,
		    fu_chunk_get_address(chk));
		buf_res = fu_steelseries_fizz_request_response(self, st_req, error);
		if (buf_res == NULL)
			return FALSE;
		st_res = fu_struct_steelseries_fizz_read_access_file_res_parse(buf_res->data,
									       buf_res->len,
									       0x0,
									       error);
		if (st_res == NULL)
			return FALSE;
		data = fu_struct_steelseries_fizz_read_access_file_res_get_data(st_res, &datasz);
		if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    data,
				    datasz,
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_get_paired_status(FuSteelseriesFizz *self, guint8 *status, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_steelseries_fizz_impl_get_paired_status(FU_STEELSERIES_FIZZ_IMPL(proxy),
							  status,
							  error);
}

gboolean
fu_steelseries_fizz_get_connection_status(FuSteelseriesFizz *self,
					  FuSteelseriesFizzConnectionStatus *status,
					  GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_steelseries_fizz_impl_get_connection_status(FU_STEELSERIES_FIZZ_IMPL(proxy),
							      status,
							      error);
}

static gboolean
fu_steelseries_fizz_ensure_children(FuSteelseriesFizz *self, GError **error)
{
	guint8 status;

	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	/* not a USB receiver */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER))
		return TRUE;

	/* in bootloader mode */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_steelseries_fizz_get_paired_status(self, &status, error)) {
		g_prefix_error(error, "failed to get paired status: ");
		return FALSE;
	}

	if (status != 0) {
		g_autoptr(FuSteelseriesFizzTunnel) paired_device =
		    fu_steelseries_fizz_tunnel_new(self);

		fu_device_set_proxy(FU_DEVICE(paired_device), FU_DEVICE(proxy));
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(paired_device));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSteelseriesFizz *self = FU_STEELSERIES_FIZZ(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER) ||
	    !fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_DETACH_BOOTLOADER))
		return TRUE;

	/* switch to bootloader mode only if device needs it */
	if (!fu_steelseries_fizz_reset(self,
				       FALSE,
				       FU_STEELSERIES_FIZZ_RESET_MODE_BOOTLOADER,
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
	FuSteelseriesFizz *self = FU_STEELSERIES_FIZZ(device);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_steelseries_fizz_reset(self,
				       FALSE,
				       FU_STEELSERIES_FIZZ_RESET_MODE_NORMAL,
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
	FuSteelseriesFizz *self = FU_STEELSERIES_FIZZ(device);
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
		if (!fu_steelseries_fizz_ensure_children(self, error))
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
		if (serial != NULL)
			fu_device_set_serial(device, serial);
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	}
	/* success */
	return TRUE;
}

gboolean
fu_steelseries_fizz_write_firmware_fs(FuSteelseriesFizz *self,
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
	if (!fu_steelseries_fizz_erase_fs(self, tunnel, fs, id, error)) {
		g_prefix_error(error, "failed to erase FS 0x%02x ID 0x%02x: ", fs, id);
		return FALSE;
	}
	fu_progress_step_done(progress);
	if (!fu_steelseries_fizz_write_fs(self,
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

	if (!fu_steelseries_fizz_get_crc32_fs(self,
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
			  fu_device_get_name(FU_DEVICE(self)),
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
	FuSteelseriesFizz *self = FU_STEELSERIES_FIZZ(device);
	guint8 fs;
	gboolean is_receiver = FALSE;
	FuDevice *proxy = fu_device_get_proxy(device);
	guint8 id = 0;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	is_receiver = fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER);
	if (!fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy),
						is_receiver,
						&fs,
						error))
		return FALSE;
	if (!fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy),
						  is_receiver,
						  &id,
						  error))
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, 1);

	if (!fu_steelseries_fizz_write_firmware_fs(self,
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
fu_steelseries_fizz_read_firmware_fs(FuSteelseriesFizz *self,
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
	if (!fu_steelseries_fizz_read_fs(self,
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
	if (!fu_firmware_parse_bytes(firmware, blob, 0x0, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static FuFirmware *
fu_steelseries_fizz_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSteelseriesFizz *self = FU_STEELSERIES_FIZZ(device);
	guint8 fs = 0;
	guint8 id = 0;
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
	if (!fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy),
						is_receiver,
						&fs,
						error))
		return NULL;
	if (!fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy),
						  is_receiver,
						  &id,
						  error))
		return NULL;

	firmware = fu_steelseries_fizz_read_firmware_fs(self,
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
fu_steelseries_fizz_class_init(FuSteelseriesFizzClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_steelseries_fizz_detach;
	device_class->attach = fu_steelseries_fizz_attach;
	device_class->setup = fu_steelseries_fizz_setup;
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
