/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-s5gen2-device.h"
#include "fu-qc-s5gen2-firmware.h"
#include "fu-qc-s5gen2-impl.h"
#include "fu-qc-s5gen2-struct.h"

#define FU_QC_S5GEN2_DEVICE_DATA_REQ_SLEEP 1000 /* ms */
#define FU_QC_S5GEN2_DEVICE_SEND_DELAY	   2	/* ms */

/* 100ms delay requested by device as a rule */
#define FU_QC_S5GEN2_DEVICE_VALIDATION_RETRIES (60000 / 100)

struct _FuQcS5gen2Device {
	FuDevice parent_instance;
	guint32 file_id;
	guint8 file_version;
	guint16 battery_raw;
	FuQcResumePoint resume_point;
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
fu_qc_s5gen2_device_msg_in(FuQcS5gen2Device *self,
			   guint8 *buf,
			   gsize bufsz,
			   gsize *read_len,
			   GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	g_autoptr(GByteArray) err_msg = NULL;
	g_autoptr(GError) error_local = NULL;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	if (!fu_qc_s5gen2_impl_msg_in(FU_QC_S5GEN2_IMPL(proxy), buf, bufsz, read_len, error))
		return FALSE;

	if (*read_len > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "read 0x%x bytes, buffer is 0x%x",
			    (guint)*read_len,
			    (guint)bufsz);
		return FALSE;
	}

	/* error detected */
	err_msg = fu_struct_qc_error_ind_parse(buf, *read_len, 0, &error_local);
	if (err_msg != NULL) {
		guint16 code = fu_struct_qc_error_ind_get_error_code(err_msg);
		g_autoptr(GByteArray) confirm = fu_struct_qc_error_res_new();

		/* confirm and stop */
		fu_struct_qc_error_res_set_error_code(confirm, code);
		if (!fu_qc_s5gen2_impl_msg_out(FU_QC_S5GEN2_IMPL(proxy),
					       confirm->data,
					       confirm->len,
					       error))
			return FALSE;

		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected error (0x%x)",
			    code);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_req_disconnect(FuQcS5gen2Device *self, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_qc_s5gen2_impl_req_disconnect(FU_QC_S5GEN2_IMPL(proxy), error);
}

static gboolean
fu_qc_s5gen2_device_cmd_req_connect(FuQcS5gen2Device *self, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_qc_s5gen2_impl_req_connect(FU_QC_S5GEN2_IMPL(proxy), error);
}

/* variable data amount depending on channel */
static gboolean
fu_qc_s5gen2_device_data_size(FuQcS5gen2Device *self, gsize *data_sz, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_qc_s5gen2_impl_data_size(FU_QC_S5GEN2_IMPL(proxy), data_sz, error);
}

static gboolean
fu_qc_s5gen2_device_cmd_abort(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_ABORT_SIZE] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_abort_req_new();
	g_autoptr(GByteArray) reply = NULL;

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), &read_len, error))
		return FALSE;

	reply = fu_struct_qc_abort_parse(data, read_len, 0, error);
	if (reply == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_sync(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_SYNC_SIZE] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_sync_req_new();
	g_autoptr(GByteArray) reply = NULL;

	fu_struct_qc_sync_req_set_file_id(req, self->file_id);
	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), &read_len, error))
		return FALSE;

	reply = fu_struct_qc_sync_parse(data, read_len, 0, error);
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

	if (self->file_id != fu_struct_qc_sync_get_file_id(reply)) {
		g_autoptr(GError) error_local = NULL;
		/* reset the update state */
		if (!fu_qc_s5gen2_device_cmd_abort(self, &error_local))
			g_debug("failed to abort: %s", error_local->message);

		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected file ID from the device (%u), expected (%u)",
			    fu_struct_qc_sync_get_file_id(reply),
			    self->file_id);

		return FALSE;
	}

	self->resume_point = fu_struct_qc_sync_get_resume_point(reply);
	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_start(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_START_SIZE] = {0};
	gsize read_len;
	FuQcStartStatus status;
	g_autoptr(GByteArray) req = fu_struct_qc_start_req_new();
	g_autoptr(GByteArray) reply = NULL;

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), &read_len, error))
		return FALSE;

	reply = fu_struct_qc_start_parse(data, read_len, 0, error);
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

	/* mostly for debug: save raw battery level */
	self->battery_raw = fu_struct_qc_start_get_battery_level(reply);

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
	guint16 delay_ms;
	guint8 data[FU_STRUCT_QC_IS_VALIDATION_DONE_SIZE] = {0};
	gsize read_len = 0;
	g_autoptr(GByteArray) req = fu_struct_qc_validation_req_new();
	g_autoptr(GByteArray) reply = NULL;
	g_autoptr(GError) error_local = NULL;

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), &read_len, error))
		return FALSE;

	if (read_len > sizeof(data)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "read 0x%x bytes, larger than inbound buffer (0x%x bytes)",
			    (guint)read_len,
			    (guint)FU_STRUCT_QC_VALIDATION_REQ_SIZE);
		return FALSE;
	}

	/* ignore the error */
	reply = fu_struct_qc_transfer_complete_ind_parse(data, sizeof(data), 0, &error_local);
	/* check if validation is complete */
	if (reply != NULL)
		return TRUE;

	reply = fu_struct_qc_is_validation_done_parse(data, sizeof(data), 0, error);
	if (reply == NULL)
		return FALSE;

	delay_ms = fu_struct_qc_is_validation_done_get_delay(reply);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "validation of the image is not complete, waiting (%u) ms",
		    delay_ms);
	fu_device_sleep(FU_DEVICE(self), delay_ms);
	return FALSE;
}

static gboolean
fu_qc_s5gen2_device_validation_cb(FuDevice *device, gpointer user_data, GError **error)
{
	return fu_qc_s5gen2_device_cmd_validation(FU_QC_S5GEN2_DEVICE(device), error);
}

static gboolean
fu_qc_s5gen2_device_cmd_transfer_complete(FuQcS5gen2Device *self, GError **error)
{
	/* reboot immediately */
	FuQcTransferAction action = FU_QC_TRANSFER_ACTION_INTERACTIVE;
	g_autoptr(GByteArray) req = fu_struct_qc_transfer_complete_new();

	fu_struct_qc_transfer_complete_set_action(req, action);

	/* if reboot immediately, the write might return error */
	return fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error);
}

static gboolean
fu_qc_s5gen2_device_cmd_proceed_to_commit(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_COMMIT_REQ_SIZE] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_proceed_to_commit_new();
	g_autoptr(GByteArray) reply = NULL;

	fu_struct_qc_proceed_to_commit_set_action(req, FU_QC_COMMIT_ACTION_PROCEED);

	if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), &read_len, error))
		return FALSE;

	reply = fu_struct_qc_commit_req_parse(data, read_len, 0, error);
	if (reply == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_cmd_commit_cfm(FuQcS5gen2Device *self, GError **error)
{
	guint8 data[FU_STRUCT_QC_COMPLETE_SIZE] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_commit_cfm_new();
	g_autoptr(GByteArray) reply = NULL;

	fu_struct_qc_commit_cfm_set_action(req, FU_QC_COMMIT_CFM_ACTION_UPGRADE);

	if (self->resume_point != FU_QC_RESUME_POINT_POST_COMMIT) {
		if (!fu_qc_s5gen2_device_msg_out(self, req->data, req->len, error))
			return FALSE;
	}

	if (!fu_qc_s5gen2_device_msg_in(self, data, sizeof(data), &read_len, error))
		return FALSE;

	reply = fu_struct_qc_complete_parse(data, read_len, 0, error);
	if (reply == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_ensure_version(FuQcS5gen2Device *self, GError **error)
{
	guint8 ver_raw[FU_STRUCT_QC_VERSION_SIZE] = {0};
	g_autofree gchar *ver_str = NULL;
	gsize read_len;
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
	if (!fu_qc_s5gen2_device_msg_in(self, ver_raw, sizeof(ver_raw), &read_len, error))
		return FALSE;
	version = fu_struct_qc_version_parse(ver_raw, read_len, 0, error);
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

	g_debug("resume point: %s", fu_qc_resume_point_to_string(self->resume_point));
	if (self->resume_point != FU_QC_RESUME_POINT_POST_REBOOT &&
	    self->resume_point != FU_QC_RESUME_POINT_COMMIT &&
	    self->resume_point != FU_QC_RESUME_POINT_POST_COMMIT) {
		g_autoptr(GError) error_local = NULL;
		/* reset the update state */
		if (!fu_qc_s5gen2_device_cmd_abort(self, &error_local))
			g_debug("failed to abort: %s", error_local->message);

		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unexpected resume point (%s)",
			    fu_qc_resume_point_to_string(self->resume_point));
		return FALSE;
	}

	if (self->resume_point == FU_QC_RESUME_POINT_POST_REBOOT) {
		if (!fu_qc_s5gen2_device_cmd_proceed_to_commit(self, error)) {
			g_prefix_error(error, "failed to cmd-proceed-to-commit: ");
			return FALSE;
		}
		self->resume_point = FU_QC_RESUME_POINT_COMMIT;
	}

	g_debug("resume point: %s", fu_qc_resume_point_to_string(self->resume_point));
	if (!fu_qc_s5gen2_device_cmd_commit_cfm(self, error)) {
		g_prefix_error(error, "failed to cmd-commit: ");
		return FALSE;
	}
	self->resume_point = FU_QC_RESUME_POINT_POST_COMMIT;
	g_debug("resume point: %s", fu_qc_resume_point_to_string(self->resume_point));

	/* success */
	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_reload(FuDevice *device, GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	if (!fu_qc_s5gen2_device_ensure_version(self, error)) {
		g_prefix_error(error, "failed to ensure version on reload: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_setup(FuDevice *device, GError **error)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	if (!fu_qc_s5gen2_device_ensure_version(self, error)) {
		g_prefix_error(error, "failed to ensure version: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_qc_s5gen2_device_write_bucket(FuQcS5gen2Device *self,
				 GBytes *data,
				 FuQcMoreData last,
				 GError **error)
{
	gsize data_sz = 0;
	g_autoptr(FuChunkArray) chunks = NULL;

	if (!fu_qc_s5gen2_device_data_size(self, &data_sz, error))
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes(data,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       data_sz);

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

		pkt = g_byte_array_append(pkt, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		if (pkt == NULL)
			return FALSE;

		if (!fu_qc_s5gen2_device_msg_out(self,
						 pkt->data,
						 FU_STRUCT_QC_DATA_SIZE + fu_chunk_get_data_sz(chk),
						 error))
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
		gsize read_len;
		guint32 data_sz;
		guint32 data_offset;
		g_autoptr(GByteArray) data_req = NULL;
		g_autoptr(GBytes) data_out = NULL;

		if (!fu_qc_s5gen2_device_msg_in(self, buf_in, sizeof(buf_in), &read_len, error))
			return FALSE;
		data_req = fu_struct_qc_data_req_parse(buf_in, read_len, 0, error);
		if (data_req == NULL)
			return FALSE;

		/* requested data */
		data_sz = fu_struct_qc_data_req_get_fw_data_len(data_req);
		data_offset = fu_struct_qc_data_req_get_fw_data_offset(data_req);

		/* FIXME: abort for now */
		if (data_sz == 0) {
			g_autoptr(GError) error_local = NULL;
			/* reset the update state */
			if (!fu_qc_s5gen2_device_cmd_abort(self, &error_local))
				g_debug("failed to abort: %s", error_local->message);

			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "requested 0x%x bytes",
				    (guint)data_sz);
			return FALSE;
		}

		cur_offset += data_offset;
		/* requested data might be larger than the single packet payload */
		if (blobsz < (cur_offset + data_sz)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unexpected firmware data requested: offset=%u, size=%u",
				    cur_offset,
				    data_sz);
			return FALSE;
		}

		more_data = (blobsz <= (cur_offset + data_sz)) ? FU_QC_MORE_DATA_LAST_PACKET
							       : FU_QC_MORE_DATA_MORE;

		data_out = g_bytes_new_from_bytes(bytes, cur_offset, data_sz);

		if (!fu_qc_s5gen2_device_write_bucket(self, data_out, more_data, error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress, data_sz + cur_offset, blobsz);

		cur_offset += data_sz;
		g_debug("written 0x%x bytes of 0x%x", (guint)cur_offset, (guint)blobsz);

		/* FIXME: petentially infinite loop if device requesting wrong data?
		   some counter or timeout? */
	} while (more_data != FU_QC_MORE_DATA_LAST_PACKET);

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
	/* sync requires ID of the firmware calculated */
	if (!fu_qc_s5gen2_device_cmd_sync(self, error))
		return FALSE;

	if (self->resume_point == FU_QC_RESUME_POINT_START) {
		/* reset the update state for the case if data partially written */
		if (!fu_qc_s5gen2_device_cmd_abort(self, error))
			return FALSE;
		if (!fu_qc_s5gen2_device_cmd_sync(self, error))
			return FALSE;
	}

	if (!fu_qc_s5gen2_device_cmd_start(self, error))
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 83, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 17, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	g_debug("resume point: %s", fu_qc_resume_point_to_string(self->resume_point));
	if (self->resume_point == FU_QC_RESUME_POINT_START) {
		if (!fu_qc_s5gen2_device_cmd_start_data(self, error))
			return FALSE;

		if (!fu_qc_s5gen2_device_write_blocks(self,
						      fw,
						      fu_progress_get_child(progress),
						      error))
			return FALSE;

		self->resume_point = FU_QC_RESUME_POINT_PRE_VALIDATE;
	}
	fu_progress_step_done(progress);

	g_debug("resume point: %s", fu_qc_resume_point_to_string(self->resume_point));
	if (self->resume_point == FU_QC_RESUME_POINT_PRE_VALIDATE) {
		/* send validation request */
		/* get the FU_QC_OPCODE_TRANSFER_COMPLETE_IND during 60000ms or fail */
		if (!fu_device_retry_full(device,
					  fu_qc_s5gen2_device_validation_cb,
					  FU_QC_S5GEN2_DEVICE_VALIDATION_RETRIES,
					  0, /* custom delay based on value in response */
					  NULL,
					  error))
			return FALSE;

		self->resume_point = FU_QC_RESUME_POINT_PRE_REBOOT;
	}

	fu_progress_step_done(progress);

	g_debug("resume point: %s", fu_qc_resume_point_to_string(self->resume_point));
	if (self->resume_point == FU_QC_RESUME_POINT_PRE_REBOOT) {
		/* complete & reboot the device */
		g_autoptr(GError) error_local = NULL;

		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_qc_s5gen2_device_cmd_transfer_complete(self, &error_local);

		if (error_local != NULL)
			g_debug("expected error during auto reboot: %s", error_local->message);

		self->resume_point = FU_QC_RESUME_POINT_POST_REBOOT;
	}

	return TRUE;
}

static void
fu_qc_s5gen2_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_qc_s5gen2_device_replace(FuDevice *device, FuDevice *donor)
{
	FuQcS5gen2Device *self = FU_QC_S5GEN2_DEVICE(device);
	FuQcS5gen2Device *self_donor = FU_QC_S5GEN2_DEVICE(donor);
	self->file_id = self_donor->file_id;
	self->file_version = self_donor->file_version;
	self->battery_raw = self_donor->battery_raw;
	self->resume_point = self_donor->resume_point;
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
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
}

static void
fu_qc_s5gen2_device_class_init(FuQcS5gen2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_qc_s5gen2_device_to_string;
	device_class->setup = fu_qc_s5gen2_device_setup;
	device_class->reload = fu_qc_s5gen2_device_reload;
	device_class->attach = fu_qc_s5gen2_device_attach;
	device_class->prepare_firmware = fu_qc_s5gen2_device_prepare_firmware;
	device_class->write_firmware = fu_qc_s5gen2_device_write_firmware;
	device_class->set_progress = fu_qc_s5gen2_device_set_progress;
	device_class->replace = fu_qc_s5gen2_device_replace;
}
