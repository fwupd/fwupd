/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-lenovo-accessory-ble-device.h"
#include "fu-lenovo-accessory-firmware.h"
#include "fu-lenovo-accessory-impl.h"

struct _FuLenovoAccessoryBleDevice {
	FuBluezDevice parent_instance;
	FuIOChannel *notify_io; /* owned, for AcquireNotify mode */
};

static void
fu_lenovo_accessory_ble_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuLenovoAccessoryBleDevice,
			fu_lenovo_accessory_ble_device,
			FU_TYPE_BLUEZ_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_LENOVO_ACCESSORY_IMPL,
					      fu_lenovo_accessory_ble_device_impl_iface_init))

/* the signature blob is transferred in the same chunk size as the payload */
#define FU_LENOVO_ACCESSORY_BLE_SIGNATURE_CHUNK_SZ 32

/*
 * Hashing the image on-device can take up to ten seconds on the slowest
 * hardware; the protocol spec recommends re-checking at a 100ms interval.
 */
#define FU_LENOVO_ACCESSORY_BLE_SIGNATURE_VERIFY_COUNT 150
#define FU_LENOVO_ACCESSORY_BLE_SIGNATURE_VERIFY_DELAY 100 /* ms */

#define UUID_WRITE "c1d02501-2d1f-400a-95d2-6a2f7bca0c25"
#define UUID_READ  "c1d02502-2d1f-400a-95d2-6a2f7bca0c25"

#define FU_LENOVO_ACCESSORY_BLE_NOTIFY_BUFSZ   64    /* bytes */
#define FU_LENOVO_ACCESSORY_BLE_NOTIFY_TIMEOUT 10000 /* ms */

static gboolean
fu_lenovo_accessory_ble_device_write_files(FuLenovoAccessoryBleDevice *self,
					   FuLenovoAccessoryDfuFileType file_type,
					   FuInputStream *stream,
					   FuProgress *progress,
					   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream, 0, 0, 32, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_lenovo_accessory_impl_dfu_file(FU_LENOVO_ACCESSORY_IMPL(self),
						       file_type,
						       fu_chunk_get_address(chk),
						       fu_chunk_get_data(chk),
						       fu_chunk_get_data_sz(chk),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

typedef struct {
	FuLenovoAccessorySignatureAlgo algo;
} FuLenovoAccessoryBleVerifyHelper;

static gboolean
fu_lenovo_accessory_ble_device_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error);

/* read the verify result; the request was already sent by the caller */
static gboolean
fu_lenovo_accessory_ble_device_verify_cb(FuDevice *device,
					 gpointer user_data G_GNUC_UNUSED,
					 GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(device);
	FuLenovoAccessoryVerifyResult result;
	guint8 target_status;
	FuLenovoAccessoryStatus status;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;
	g_autoptr(FuStructLenovoDfuSignatureVerifyRsp) st_rsp = NULL;

	/* read response based on mode */
	if (self->notify_io != NULL) {
		/* notify mode: read from the notify stream; the device pushes the
		 * packet arrives rather than waiting for the read to time out */
		buf = fu_io_channel_read_byte_array(self->notify_io,
						    -1,
						    FU_LENOVO_ACCESSORY_BLE_NOTIFY_TIMEOUT,
						    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						    error);
		if (buf == NULL) {
			g_prefix_error_literal(error, "failed to read notify: ");
			return FALSE;
		}
	} else {
		/* active read mode */
		buf = fu_bluez_device_read(FU_BLUEZ_DEVICE(device), UUID_READ, error);
		if (buf == NULL)
			return FALSE;
	}

	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, 0x0, error);
	if (st_cmd == NULL)
		return FALSE;

	/* the device reports the command as busy while it hashes the image */
	target_status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd);
	status = target_status & 0x0F;
	if (status == FU_LENOVO_ACCESSORY_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_ACCESSORY_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed with status 0x%02x",
			    status);
		return FALSE;
	}

	/* the answer must echo back the command we sent */
	if (fu_struct_lenovo_accessory_cmd_get_command_class(st_cmd) !=
		FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS ||
	    fu_struct_lenovo_accessory_cmd_get_command_id(st_cmd) !=
		(FU_LENOVO_ACCESSORY_DFU_ID_DFU_SIGNATURE_VERIFY |
		 (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "stale frame");
		return FALSE;
	}

	st_rsp =
	    fu_struct_lenovo_dfu_signature_verify_rsp_parse(buf->data,
							    buf->len,
							    FU_STRUCT_LENOVO_ACCESSORY_CMD_SIZE,
							    error);
	if (st_rsp == NULL)
		return FALSE;
	result = fu_struct_lenovo_dfu_signature_verify_rsp_get_result(st_rsp);
	if (result != FU_LENOVO_ACCESSORY_VERIFY_RESULT_PASS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    result == FU_LENOVO_ACCESSORY_VERIFY_RESULT_ALGO_NOT_SUPPORTED
				? FWUPD_ERROR_NOT_SUPPORTED
				: FWUPD_ERROR_INVALID_DATA,
			    "signature verification failed: %s",
			    fu_lenovo_accessory_verify_result_to_string(result));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_write_signature_chunks(FuLenovoAccessoryBleDevice *self,
						      FuChunkArray *chunks,
						      guint16 sigsz,
						      FuProgress *progress,
						      GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_lenovo_accessory_impl_dfu_signature_file(FU_LENOVO_ACCESSORY_IMPL(self),
								 sigsz,
								 (guint16)fu_chunk_get_address(chk),
								 fu_chunk_get_data(chk),
								 fu_chunk_get_data_sz(chk),
								 error)) {
			g_prefix_error(error, "failed to write signature chunk 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_signature_verify_send(FuLenovoAccessoryBleDevice *self,
						     FuLenovoAccessorySignatureAlgo algo,
						     GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDfuSignatureVerifyReq) st_req =
	    fu_struct_lenovo_dfu_signature_verify_req_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x02);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_SIGNATURE_VERIFY |
		(FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_dfu_signature_verify_req_set_cmd(st_req, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_dfu_signature_verify_req_set_algo(st_req, algo);
	if (!fu_lenovo_accessory_ble_device_write(FU_LENOVO_ACCESSORY_IMPL(self),
						  st_req->buf,
						  error)) {
		g_prefix_error_literal(error, "failed to write verify request: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_write_signature(FuLenovoAccessoryBleDevice *self,
					       FuFirmware *firmware,
					       FuProgress *progress,
					       GError **error)
{
	FuLenovoAccessorySignatureAlgo algo;
	gsize sigsz;
	FuLenovoAccessoryBleVerifyHelper helper = {0};
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuInputStream) stream_sig = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, "write-signature");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 80, "verify-signature");

	/* a bare image is still accepted by the device */
	if (!FU_IS_LENOVO_ACCESSORY_FIRMWARE(firmware)) {
		g_debug("bare image, not verifying");
		fu_progress_finished(progress);
		return TRUE;
	}
	stream_sig = fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_SIGNATURE, NULL);
	if (stream_sig == NULL) {
		g_debug("no detached signature, not verifying");
		fu_progress_finished(progress);
		return TRUE;
	}
	algo = fu_lenovo_accessory_firmware_get_algo(FU_LENOVO_ACCESSORY_FIRMWARE(firmware));
	if (!fu_input_stream_size(stream_sig, &sigsz, error))
		return FALSE;
	if (sigsz == 0 || sigsz > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "signature size %" G_GSIZE_FORMAT " is out of range",
			    sigsz);
		return FALSE;
	}

	/* the signature is transferred in the same chunk size as the payload */
	chunks = fu_chunk_array_new_from_stream(stream_sig,
						0x0,
						0x0,
						FU_LENOVO_ACCESSORY_BLE_SIGNATURE_CHUNK_SZ,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_lenovo_accessory_ble_device_write_signature_chunks(self,
								   chunks,
								   (guint16)sigsz,
								   fu_progress_get_child(progress),
								   error))
		return FALSE;
	fu_progress_step_done(progress);

	/*
	 * The device hashes the whole image before it can answer, which can
	 * take several seconds depending on the image size and the MCU. It
	 * reports the command as busy until then, so keep re-checking rather
	 * than treating the first busy answer as a failure.
	 *
	 * Send the verify request, wait 500ms to give the device time to compute
	 * the hash, then poll for the response with extended retry parameters.
	 */
	if (!fu_lenovo_accessory_ble_device_signature_verify_send(self, algo, error))
		return FALSE;

	/* give the device time to compute the hash */
	fu_device_sleep(FU_DEVICE(self), 500);

	/* poll for the response with extended retry parameters */
	helper.algo = algo;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_lenovo_accessory_ble_device_verify_cb,
				  FU_LENOVO_ACCESSORY_BLE_SIGNATURE_VERIFY_COUNT,
				  FU_LENOVO_ACCESSORY_BLE_SIGNATURE_VERIFY_DELAY,
				  &helper,
				  error)) {
		g_prefix_error_literal(error, "failed to verify signature: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(device);
	gsize fw_size = 0;
	guint32 file_crc = 0xFFFFFFFF;
	guint32 device_crc = 0;
	FuLenovoAccessoryDeviceMode mode;
	g_autoptr(FuInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 3, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 87, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, "signature");

	/* the archive layout exports the image as the payload, and the devices
	 * shipped a bare image keep the bytes on the firmware itself */
	if (FU_IS_LENOVO_ACCESSORY_FIRMWARE(firmware)) {
		stream =
		    fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	} else {
		stream = fu_firmware_get_stream(firmware, error);
	}
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &fw_size, error))
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &file_crc, error))
		return FALSE;

	/* only enter DFU mode if not already there */
	if (!fu_lenovo_accessory_impl_get_mode(FU_LENOVO_ACCESSORY_IMPL(device), &mode, error))
		return FALSE;
	if (mode != FU_LENOVO_ACCESSORY_DEVICE_MODE_DFU_MODE) {
		if (!fu_lenovo_accessory_impl_dfu_entry(FU_LENOVO_ACCESSORY_IMPL(device), error))
			return FALSE;
	}
	if (!fu_lenovo_accessory_impl_dfu_attribute(FU_LENOVO_ACCESSORY_IMPL(device),
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    error))
		return FALSE;
	if (!fu_lenovo_accessory_impl_dfu_prepare(FU_LENOVO_ACCESSORY_IMPL(device),
						  FU_LENOVO_ACCESSORY_DFU_FILE_TYPE_BIN_FILE,
						  0x0,
						  (guint32)fw_size,
						  file_crc,
						  error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_lenovo_accessory_ble_device_write_files(self,
							FU_LENOVO_ACCESSORY_DFU_FILE_TYPE_BIN_FILE,
							stream,
							fu_progress_get_child(progress),
							error))
		return FALSE;

	/* give the device time to finalize the flash before reading back CRC */
	fu_device_sleep(FU_DEVICE(self), 2000);
	if (!fu_lenovo_accessory_impl_dfu_crc(FU_LENOVO_ACCESSORY_IMPL(device),
					      &device_crc,
					      error)) {
		g_prefix_error(error, "BLE CRC Error (device 0x%08x): ", device_crc);
		return FALSE;
	}
	if (device_crc != file_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "CRC mismatch: device 0x%08x != file 0x%08x",
			    device_crc,
			    file_crc);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write and verify the detached signature, when the payload has one */
	if (!fu_lenovo_accessory_ble_device_write_signature(self,
							    firmware,
							    fu_progress_get_child(progress),
							    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(device);

	/* release the notify stream so that BlueZ can detect the disconnect */
	g_clear_object(&self->notify_io);

	if (!fu_lenovo_accessory_impl_dfu_exit(FU_LENOVO_ACCESSORY_IMPL(device),
					       FU_LENOVO_ACCESSORY_DFU_EXIT_CODE_DFU_SUCCESS,
					       error)) {
		g_prefix_error_literal(error, "failed to exit: ");
		return FALSE;
	}

	/* the device reboots and reconnects with the same address */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_setup(FuDevice *device, GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(device);
	guint8 major = 0;
	guint8 minor = 0;
	guint8 micro = 0;
	g_autofree gchar *version = NULL;

	/* if using notify mode, acquire the notify fd */
	if (fu_device_has_private_flag(device, FU_LENOVO_ACCESSORY_BLE_DEVICE_FLAG_USE_NOTIFY)) {
		gint32 mtu = 0;
		self->notify_io =
		    fu_bluez_device_notify_acquire(FU_BLUEZ_DEVICE(self), UUID_READ, &mtu, error);
		if (self->notify_io == NULL) {
			g_prefix_error_literal(error, "failed to acquire notify: ");
			return FALSE;
		}
	}

	if (!fu_lenovo_accessory_impl_get_fwversion(FU_LENOVO_ACCESSORY_IMPL(device),
						    &major,
						    &minor,
						    &micro,
						    error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, micro);
	fu_device_set_version(device, version);
	return TRUE;
}

static void
fu_lenovo_accessory_ble_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static GByteArray *
fu_lenovo_accessory_ble_device_read(FuLenovoAccessoryImpl *impl, GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(impl);
	return fu_bluez_device_read(FU_BLUEZ_DEVICE(self), UUID_READ, error);
}

static gboolean
fu_lenovo_accessory_ble_device_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(impl);
	g_autoptr(GByteArray) buf_padded = g_byte_array_new();
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	/* the peripheral rejects a request whose written length is not exactly the header plus
	 * the advertised data_size -- HID gets this for free from the fixed feature report
	 * length, but a GATT write only carries the bytes we pass in, so pad it here */
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, 0x0, error);
	if (st_cmd == NULL)
		return FALSE;
	g_byte_array_append(buf_padded, buf->data, buf->len);
	fu_byte_array_set_size(buf_padded,
			       FU_STRUCT_LENOVO_ACCESSORY_CMD_SIZE +
				   fu_struct_lenovo_accessory_cmd_get_data_size(st_cmd),
			       0x0);
	return fu_bluez_device_write(FU_BLUEZ_DEVICE(self), UUID_WRITE, buf_padded, error);
}

static gboolean
fu_lenovo_accessory_ble_device_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf_rsp = (GByteArray *)user_data;
	FuLenovoAccessoryStatus status;
	gsize offset = 0x0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	buf = fu_lenovo_accessory_ble_device_read(FU_LENOVO_ACCESSORY_IMPL(device), error);
	if (buf == NULL)
		return FALSE;
	if (buf->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "received empty data");
		return FALSE;
	}
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, offset, error);
	if (st_cmd == NULL)
		return FALSE;
	status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd) & 0x0F;
	if (status == FU_LENOVO_ACCESSORY_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_ACCESSORY_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed with status 0x%02x",
			    status);
		return FALSE;
	}
	offset += FU_STRUCT_LENOVO_ACCESSORY_CMD_SIZE;

	/* success */
	g_byte_array_append(buf_rsp, buf->data + offset, buf->len - offset);
	return TRUE;
}

static GByteArray *
fu_lenovo_accessory_ble_device_process(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(impl);
	FuLenovoAccessoryStatus status;
	gsize offset = 0x0;
	g_autoptr(GByteArray) buf_rsp = g_byte_array_new();
	g_autoptr(GByteArray) buf_read = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	/* write command */
	if (!fu_lenovo_accessory_ble_device_write(impl, buf, error)) {
		g_prefix_error_literal(error, "failed to write cmd: ");
		return NULL;
	}

	/* read response based on mode */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_LENOVO_ACCESSORY_BLE_DEVICE_FLAG_USE_NOTIFY)) {
		/* one notification is one complete response, so return as soon as the first
		 * packet arrives rather than waiting for the read to time out */
		buf_read = fu_io_channel_read_byte_array(self->notify_io,
							 FU_LENOVO_ACCESSORY_BLE_NOTIFY_BUFSZ,
							 FU_LENOVO_ACCESSORY_BLE_NOTIFY_TIMEOUT,
							 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
							 error);
		if (buf_read == NULL) {
			g_prefix_error_literal(error, "failed to read notify: ");
			return NULL;
		}
	} else {
		/* traditional polling mode: retry ReadValue until data arrives */
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_lenovo_accessory_ble_device_poll_cb,
					  50, /* count */
					  10, /* ms */
					  buf_rsp,
					  error))
			return NULL;
		return g_steal_pointer(&buf_rsp);
	}

	/* parse and validate the response (notify mode) */
	if (buf_read->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "received empty data");
		return NULL;
	}
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf_read->data, buf_read->len, offset, error);
	if (st_cmd == NULL)
		return NULL;
	status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd) & 0x0F;
	if (status == FU_LENOVO_ACCESSORY_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return NULL;
	}
	if (status != FU_LENOVO_ACCESSORY_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed with status 0x%02x",
			    status);
		return NULL;
	}
	offset += FU_STRUCT_LENOVO_ACCESSORY_CMD_SIZE;

	/* extract payload */
	g_byte_array_append(buf_rsp, buf_read->data + offset, buf_read->len - offset);
	return g_steal_pointer(&buf_rsp);
}

static void
fu_lenovo_accessory_ble_device_finalize(GObject *object)
{
	FuLenovoAccessoryBleDevice *self = FU_LENOVO_ACCESSORY_BLE_DEVICE(object);
	if (self->notify_io != NULL)
		g_object_unref(self->notify_io);
	G_OBJECT_CLASS(fu_lenovo_accessory_ble_device_parent_class)->finalize(object);
}

static void
fu_lenovo_accessory_ble_device_init(FuLenovoAccessoryBleDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* ms */
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.accessory");
	fu_device_set_install_duration(FU_DEVICE(self), 60);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
}

static void
fu_lenovo_accessory_ble_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface)
{
	iface->read = fu_lenovo_accessory_ble_device_read;
	iface->write = fu_lenovo_accessory_ble_device_write;
	iface->process = fu_lenovo_accessory_ble_device_process;
}

static void
fu_lenovo_accessory_ble_device_class_init(FuLenovoAccessoryBleDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_lenovo_accessory_ble_device_finalize;
	device_class->write_firmware = fu_lenovo_accessory_ble_device_write_firmware;
	device_class->set_progress = fu_lenovo_accessory_ble_device_set_progress;
	device_class->setup = fu_lenovo_accessory_ble_device_setup;
	device_class->attach = fu_lenovo_accessory_ble_device_attach;
	fu_device_register_private_flag(device_class,
					FU_LENOVO_ACCESSORY_BLE_DEVICE_FLAG_USE_NOTIFY);
}
