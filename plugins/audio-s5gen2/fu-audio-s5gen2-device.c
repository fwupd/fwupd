/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-audio-s5gen2-device.h"
#include "fu-audio-s5gen2-firmware.h"
#include "fu-audio-s5gen2-impl.h"
#include "fu-audio-s5gen2-struct.h"

#define FU_QC_S5GEN2_DEVICE_DATA_REQ_SLEEP 1000 /* ms */
#define FU_QC_S5GEN2_DEVICE_SEND_DELAY	   2	/* ms */

/* 100ms delay requested by device as a rule */
#define FU_QC_S5GEN2_DEVICE_VALIDATION_RETRIES (60000 / 100)

struct _FuQcS5gen2Device {
	FuDevice parent_instance;
	guint32 file_id;
	guint8 file_version;
	guint16 battery_raw;
};

G_DEFINE_TYPE(FuQcS5gen2Device, fu_qc_s5gen2_device, FU_TYPE_DEVICE)

static void
fu_qc_s5gen2_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FileId", self->file_id);
	fwupd_codec_string_append_hex(str, idt, "FileVersion", self->file_version);
	fwupd_codec_string_append_hex(str, idt, "BatteryRaw", self->battery_raw);
}

static gboolean
fu_qc_s5gen2_device_msg_out(FuQcS5gen2Device *self, guint8 *data, gsize data_len, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_qc_s5gen2_impl_msg_out(FU_QC_S5GEN2_IMPL(proxy), data, data_len, error);
}

static gboolean
fu_qc_s5gen2_device_msg_in(FuQcS5gen2Device *self, guint8 *data_in, gsize data_len, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_qc_s5gen2_impl_msg_in(FU_QC_S5GEN2_IMPL(proxy), data_in, data_len, error);
}

static gboolean
fu_qc_s5gen2_device_msg_cmd(FuQcS5gen2Device *self, guint8 *data, gsize data_len, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_qc_s5gen2_impl_msg_cmd(FU_QC_S5GEN2_IMPL(proxy), data, data_len, error);
}

static gboolean
fu_qc_s5gen2_device_cmd_req_disconnect(FuQcS5gen2Device *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_qc_disconnect_req_new();
	return fu_qc_s5gen2_device_msg_cmd(self, req->data, req->len, error);
}

static gboolean
fu_qc_s5gen2_device_cmd_req_connect(FuQcS5gen2Device *self, GError **error)
{
	guint8 data_in[FU_STRUCT_QC_UPDATE_STATUS_SIZE] = {0x0};
	FuQcStatus update_status;
	g_autoptr(GByteArray) req = fu_struct_qc_connect_req_new();
	g_autoptr(GByteArray) st = NULL;

	if (!fu_qc_s5gen2_device_msg_cmd(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data_in, sizeof(data_in), error))
		return FALSE;
	st = fu_struct_qc_update_status_parse(data_in, sizeof(data_in), 0, error);
	if (st == NULL)
		return FALSE;

	update_status = fu_struct_qc_update_status_get_status(st);
	switch (update_status) {
	case FU_QC_STATUS_SUCCESS:
		break;
	case FU_QC_STATUS_ALREADY_CONNECTED_WARNING:
		g_info("device is already connected");
		/* FIXME: continue the previous update for wireless
		 * atm fail for USB */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device is already connected");
		return FALSE;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid update status (%s)",
			    fu_qc_status_to_string(update_status));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_abort(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_ABORT_SIZE] = {0};
	g_autoptr(GByteArray) req = fu_struct_qc_abort_req_new();
	g_autoptr(GByteArray) reply = NULL;

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), error))
		return FALSE;

	reply = fu_struct_qc_abort_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_sync(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_SYNC_SIZE] = {0};
	FuQcResumePoint rp;
	g_autoptr(GByteArray) req = fu_struct_qc_sync_req_new();
	g_autoptr(GByteArray) reply = NULL;

	fu_struct_qc_sync_req_set_file_id(req, self->file_id);
	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), error))
		return FALSE;

	/* FIXME: correct error handling -- move to msg_in()? */
	if (data[0] == 0x11) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected error (0x%.02X)",
			    data[0]);
		return FALSE;
	}

	reply = fu_struct_qc_sync_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	if (self->file_version != fu_struct_qc_sync_get_protocol_version(reply)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unsupported firmware protocol version on device %u, expected %u",
			    fu_struct_qc_sync_get_protocol_version(reply),
			    self->file_version);
		return FALSE;
	}

	rp = fu_struct_qc_sync_get_resume_point(reply);
	switch (rp) {
	case FU_QC_RESUME_POINT_START:
	case FU_QC_RESUME_POINT_POST_REBOOT:
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected resume point (%s)",
			    fu_qc_resume_point_to_string(rp));
		return FALSE;
	}

	if (self->file_id != fu_struct_qc_sync_get_file_id(reply)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected file ID from the device (%u), expected (%u)",
			    fu_struct_qc_sync_get_file_id(reply),
			    self->file_id);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_start(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_START_SIZE] = {0};
	FuQcStartStatus status;
	g_autoptr(GByteArray) req = fu_struct_qc_start_req_new();
	g_autoptr(GByteArray) reply = NULL;

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), error))
		return FALSE;

	reply = fu_struct_qc_start_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	status = fu_struct_qc_start_get_status(reply);
	if (status != FU_QC_START_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status failure in upgrade (%s)",
			    fu_qc_start_status_to_string(status));
		return FALSE;
	}

	/* check battery */
	self->battery_raw = fu_struct_qc_start_get_battery_level(reply);

	/* FIXME: calculate and set real percentage here.
	 * For now just pass the threshold. */
	fu_device_set_battery_level(FU_DEVICE(self), 100);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_start_data(FuQcS5gen2Device *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_qc_start_data_req_new();

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), FU_QC_S5GEN2_DEVICE_DATA_REQ_SLEEP);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_validation(FuQcS5gen2Device *self, GError **error)
{
	FuQcOpcode opcode;
	guint16 delay_ms;
	guint8 data[FU_STRUCT_QC_VALIDATION_SIZE] = {0};
	g_autoptr(GByteArray) req = fu_struct_qc_validation_req_new();
	g_autoptr(GByteArray) reply = NULL;

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), error))
		return FALSE;

	/* do not care about FU_QC_OPCODE_TRANSFER_COMPLETE_IND format */
	reply = fu_struct_qc_validation_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	opcode = fu_struct_qc_validation_get_opcode(reply);
	switch (opcode) {
	case FU_QC_OPCODE_TRANSFER_COMPLETE_IND:
		break;
	case FU_QC_OPCODE_IS_VALIDATION_DONE_CFM:
		delay_ms = fu_struct_qc_validation_get_delay(reply);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "validation of the image is not complete, waiting (%u) ms",
			    delay_ms);
		fu_device_sleep(FU_DEVICE(self), delay_ms);
		return FALSE;
	default:
		fu_device_sleep(FU_DEVICE(self), FU_QC_S5GEN2_DEVICE_SEND_DELAY);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected opcode (%s)",
			    fu_qc_opcode_to_string(opcode));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_validation_cb(FuDevice *device, gpointer user_data, GError **error)
{
	return fu_qc_s5gen2_device_cmd_validation(FU_QC_S5GEN2_DEVICE(device), error);
}

static gboolean
fu_qc_s5gen2_device_cmd_transfer_complete(FuQcS5gen2Device *self, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_qc_transfer_complete_new();

	fu_struct_qc_transfer_complete_set_action(req, FU_QC_ACTION_PROCEED);

	return fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error);
}

static gboolean
fu_qc_s5gen2_device_cmd_proceed_to_commit(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_COMMIT_REQ_SIZE] = {0};
	g_autoptr(GByteArray) req = fu_struct_qc_proceed_to_commit_new();
	g_autoptr(GByteArray) reply = NULL;

	fu_struct_qc_proceed_to_commit_set_action(req, FU_QC_ACTION_PROCEED);

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), error))
		return FALSE;

	reply = fu_struct_qc_commit_req_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_commit(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_COMPLETE_SIZE] = {0};
	g_autoptr(GByteArray) req = fu_struct_qc_commit_cfm_new();
	g_autoptr(GByteArray) reply = NULL;

	fu_struct_qc_commit_cfm_set_action(req, FU_QC_COMMIT_ACTION_UPGRADE);

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), error))
		return FALSE;

	reply = fu_struct_qc_complete_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_ensure_version(FuQcS5gen2Device *self, GError **error)
{
	guint8 ver_raw[FU_STRUCT_QC_VERSION_SIZE] = {0};
	g_autofree gchar *ver_str = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GByteArray) version = NULL;
	g_autoptr(GByteArray) version_req = fu_struct_qc_version_req_new();

	locker =
	    fu_device_locker_new_full(FU_DEVICE(self),
				      (FuDeviceLockerFunc)fu_qc_s5gen2_device_cmd_req_connect,
				      (FuDeviceLockerFunc)fu_qc_s5gen2_device_cmd_req_disconnect,
				      error);
	if (locker == NULL)
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_out(self, version_req->data, version_req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, ver_raw, sizeof(ver_raw), error))
		return FALSE;
	version = fu_struct_qc_version_parse(ver_raw, sizeof(ver_raw), 0, error);
	if (version == NULL)
		return FALSE;

	ver_str = g_strdup_printf("%u.%u.%u",
				  fu_struct_qc_version_get_major(version),
				  fu_struct_qc_version_get_minor(version),
				  fu_struct_qc_version_get_config(version));
	fu_device_set_version(FU_DEVICE(self), ver_str);
	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker =
	    fu_device_locker_new_full(device,
				      (FuDeviceLockerFunc)fu_qc_s5gen2_device_cmd_req_connect,
				      (FuDeviceLockerFunc)fu_qc_s5gen2_device_cmd_req_disconnect,
				      error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to connect: ");
		return FALSE;
	}
	if (!fu_qc_s5gen2_device_cmd_sync(self, error)) {
		g_prefix_error(error, "failed to cmd-sync: ");
		return FALSE;
	}
	if (!fu_qc_s5gen2_device_cmd_start(self, error)) {
		g_prefix_error(error, "failed to cmd-start: ");
		return FALSE;
	}
	if (!fu_qc_s5gen2_device_cmd_proceed_to_commit(self, error)) {
		g_prefix_error(error, "failed to cmd-proceed-to-commit: ");
		return FALSE;
	}
	if (!fu_qc_s5gen2_device_cmd_commit(self, error)) {
		g_prefix_error(error, "failed to cmd-commit: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_reload(FuDevice *device, GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	return fu_qc_s5gen2_device_ensure_version(self, error);
}

static gboolean
fu_qc_s5gen2_device_setup(FuDevice *device, GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	return fu_qc_s5gen2_device_ensure_version(self, error);
}

static gboolean
fu_qc_s5gen2_device_prepare(FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker =
	    fu_device_locker_new_full(device,
				      (FuDeviceLockerFunc)fu_qc_s5gen2_device_cmd_req_connect,
				      (FuDeviceLockerFunc)fu_qc_s5gen2_device_cmd_req_disconnect,
				      error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to connect: ");
		return FALSE;
	}

	/* FIXME: do abort of any stalled upgrade for USB only
	 * rework that part to continue update for wireless/USB */
	if (!fu_qc_s5gen2_device_cmd_abort(self, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_write_bucket(FuQcS5gen2Device *self,
				 GBytes *data,
				 FuQcMoreData last,
				 GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(data, 0, FU_STRUCT_QC_DATA_SIZE_DATA);

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(GByteArray) pkt = fu_struct_qc_data_new();
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);

		if (chk == NULL)
			return FALSE;

		fu_struct_qc_data_set_data_len(pkt, fu_chunk_get_data_sz(chk) + 1);
		/* only the last block of the last bucket should have flag LAST */
		if ((i + 1) == fu_chunk_array_length(chunks))
			fu_struct_qc_data_set_last_packet(pkt, last);
		else
			fu_struct_qc_data_set_last_packet(pkt, FU_QC_MORE_DATA_MORE);

		if (!fu_struct_qc_data_set_data(pkt,
						fu_chunk_get_data(chk),
						fu_chunk_get_data_sz(chk),
						error))
			return FALSE;

		if (!fu_qc_s5gen2_device_msg_out(self, pkt->data, pkt->len, error))
			return FALSE;

		/* wait between packets sending */
		fu_device_sleep(FU_DEVICE(self), FU_QC_S5GEN2_DEVICE_SEND_DELAY);
	}

	return TRUE;
}
static gboolean
fu_qc_s5gen2_device_write_blocks(FuQcS5gen2Device *self,
				 GBytes *bytes,
				 FuProgress *progress,
				 GError **error)
{
	const gsize blobsz = g_bytes_get_size(bytes);
	guint32 cur_offset = 0;
	FuQcMoreData more_data = FU_QC_MORE_DATA_MORE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);

	/* device is requesting data from the host */
	do {
		guint8 buf_in[FU_STRUCT_QC_DATA_REQ_SIZE] = {0};
		guint32 data_sz;
		guint32 data_offset;
		g_autoptr(GByteArray) data_req = NULL;
		g_autoptr(GBytes) data_out = NULL;

		if (!fu_qc_s5gen2_device_msg_in(self, buf_in, sizeof(buf_in), error))
			return FALSE;
		data_req = fu_struct_qc_data_req_parse(buf_in, sizeof(buf_in), 0, error);
		if (data_req == NULL)
			return FALSE;

		/* requested data */
		data_sz = fu_struct_qc_data_req_get_fw_data_len(data_req);
		data_offset = fu_struct_qc_data_req_get_fw_data_offset(data_req);

		cur_offset += data_offset;
		/* requested data might be larger than the single packet payload */
		/* FIXME: checking the data is less or equal the firmware size? */
		if (blobsz < (cur_offset + data_sz)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unexpected firmware data requested: offset=%u, size=%u",
				    cur_offset,
				    data_sz);
			return FALSE;
		}

		more_data = (blobsz <= (cur_offset + data_sz)) ? FU_QC_MORE_DATA_LAST
							       : FU_QC_MORE_DATA_MORE;

		data_out = g_bytes_new_from_bytes(bytes, cur_offset, data_sz);

		if (!fu_qc_s5gen2_device_write_bucket(self, data_out, more_data, error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress, data_sz + cur_offset, blobsz);

		cur_offset += data_sz;

		/* FIXME: petentially infinite loop if device requesting wrong data?
		   some counter or timeout? */
	} while (more_data != FU_QC_MORE_DATA_LAST);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_qc_s5gen2_device_prepare_firmware(FuDevice *device,
				     GInputStream *stream,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_qc_s5gen2_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0, flags, error))
		return NULL;

	self->file_version =
	    fu_qc_s5gen2_firmware_get_protocol_version(FU_QC_S5GEN2_FIRMWARE(firmware));
	self->file_id = fu_qc_s5gen2_firmware_get_id(FU_QC_S5GEN2_FIRMWARE(firmware));

	return g_steal_pointer(&firmware);
}

static gboolean
fu_qc_s5gen2_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;

	if (!fu_qc_s5gen2_device_cmd_req_connect(self, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_cmd_sync(self, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_cmd_start(self, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_cmd_start_data(self, error))
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 83, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 17, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	if (!fu_qc_s5gen2_device_write_blocks(self, fw, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send validation request */
	/* get the FU_QC_OPCODE_TRANSFER_COMPLETE_IND during 60000ms or fail */
	if (!fu_device_retry_full(device,
				  fu_qc_s5gen2_device_validation_cb,
				  FU_QC_S5GEN2_DEVICE_VALIDATION_RETRIES,
				  0, /* custom delay based on value in response */
				  NULL,
				  error))
		return FALSE;

	fu_progress_step_done(progress);

	/* complete & reboot the device */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return fu_qc_s5gen2_device_cmd_transfer_complete(self, error);
}

static void
fu_qc_s5gen2_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_qc_s5gen2_hid_device_replace(FuDevice *device, FuDevice *donor)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	FuQcS5gen2Device *self_donor = FU_QC_S5GEN2_DEVICE(donor);
	self->file_id = self_donor->file_id;
	self->file_version = self_donor->file_version;
	self->battery_raw = self_donor->battery_raw;
}

static void
fu_qc_s5gen2_device_init(FuQcS5gen2Device *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_QC_S5GEN2_DEVICE_REMOVE_DELAY);
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.s5gen2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PROXY_FOR_OPEN);
}

static void
fu_qc_s5gen2_device_class_init(FuQcS5gen2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_qc_s5gen2_device_to_string;
	device_class->setup = fu_qc_s5gen2_device_setup;
	device_class->reload = fu_qc_s5gen2_device_reload;
	device_class->prepare = fu_qc_s5gen2_device_prepare;
	device_class->attach = fu_qc_s5gen2_device_attach;
	device_class->prepare_firmware = fu_qc_s5gen2_device_prepare_firmware;
	device_class->write_firmware = fu_qc_s5gen2_device_write_firmware;
	device_class->set_progress = fu_qc_s5gen2_hid_device_set_progress;
	device_class->replace = fu_qc_s5gen2_hid_device_replace;
}
