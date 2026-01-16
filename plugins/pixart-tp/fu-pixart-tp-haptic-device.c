/* Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-device.h"
#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-haptic-device.h"
#include "fu-pixart-tp-section.h"
#include "fu-pixart-tp-struct.h"

struct _FuPixartTpHapticDevice {
	FuDevice parent_instance;
	guint8 status;
	guint16 packet_number;
};

G_DEFINE_TYPE(FuPixartTpHapticDevice, fu_pixart_tp_haptic_device, FU_TYPE_DEVICE)

#define FU_PIXART_TP_TF_RETRY_COUNT 3

#define FU_PIXART_TP_TF_RETRY_INTERVAL_MS 10

#define FU_PIXART_TP_TF_FRAME_SIZE_FEATURE_REPORT_LEN 64

static void
fu_pixart_tp_haptic_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "Status", self->status);
	fwupd_codec_string_append_hex(str, idt, "PacketNumber", self->packet_number);
}

typedef struct {
	guint8 mode;
	guint8 major;
	guint8 minor;
	guint8 patch;
} FuPixartTpTfReadFwVersionCtx;

typedef struct {
	guint32 packet_total;
	guint32 packet_index;
	const guint8 *chunk_data;
	gsize chunk_len;
} FuPixartTpTfWritePacketCtx;

static gboolean
fu_pixart_tp_haptic_device_tf_write_rmi_cmd(FuPixartTpHapticDevice *self,
					    guint16 addr,
					    guint8 cmd,
					    GError **error)
{
	FuDevice *proxy;
	gsize crc_off = 2;
	guint8 crc;
	g_autoptr(FuStructPixartTpTfWriteSimpleCmd) st =
	    fu_struct_pixart_tp_tf_write_simple_cmd_new();

	fu_struct_pixart_tp_tf_write_simple_cmd_set_addr(st, addr);
	fu_struct_pixart_tp_tf_write_simple_cmd_set_len(st, sizeof(cmd));
	fu_byte_array_append_uint8(st->buf, cmd);

	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD, st->buf->data + crc_off, st->buf->len - crc_off);
	g_byte_array_append(st->buf, &crc, 1);
	fu_byte_array_append_uint8(st->buf, FU_PIXART_TP_TF_FRAME_CONST_TAIL);
	fu_byte_array_set_size(st->buf, FU_PIXART_TP_TF_FRAME_SIZE_FEATURE_REPORT_LEN, 0x00);
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(proxy),
					    st->buf->data,
					    st->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pixart_tp_haptic_device_tf_write_rmi_with_packet(FuPixartTpHapticDevice *self,
						    guint16 addr,
						    gsize packet_total,
						    gsize packet_index,
						    const guint8 *in_buf,
						    gsize in_bufsz,
						    GError **error)
{
	FuDevice *proxy;
	gsize crc_off = 2;
	guint8 crc;
	g_autoptr(FuStructPixartTpTfWritePacketCmd) st =
	    fu_struct_pixart_tp_tf_write_packet_cmd_new();

	/* protocol overhead: packet_total (2) + packet_index (2) */
	fu_struct_pixart_tp_tf_write_packet_cmd_set_addr(st, addr);
	fu_struct_pixart_tp_tf_write_packet_cmd_set_datalen(st,
							    (guint16)in_bufsz +
								sizeof(guint16) * 2);
	fu_struct_pixart_tp_tf_write_packet_cmd_set_packet_total(st, (guint16)packet_total);
	fu_struct_pixart_tp_tf_write_packet_cmd_set_packet_index(st, (guint16)packet_index);

	if (in_bufsz > 0 && in_buf != NULL)
		g_byte_array_append(st->buf, in_buf, (guint)in_bufsz);

	crc = fu_crc8(FU_CRC_KIND_B8_STANDARD, st->buf->data + crc_off, st->buf->len - crc_off);
	g_byte_array_append(st->buf, &crc, 1);
	fu_byte_array_append_uint8(st->buf, FU_PIXART_TP_TF_FRAME_CONST_TAIL);
	fu_byte_array_set_size(st->buf, FU_PIXART_TP_TF_FRAME_SIZE_FEATURE_REPORT_LEN, 0x00);
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(proxy),
					    st->buf->data,
					    st->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static GByteArray *
fu_pixart_tp_haptic_device_tf_read_rmi(FuPixartTpHapticDevice *self,
				       guint16 addr,
				       const guint8 *in_buf,
				       gsize in_bufsz,
				       guint16 reply_len,
				       GError **error)
{
	FuDevice *proxy;
	gsize datalen;
	guint8 io_buf[FU_PIXART_TP_TF_FRAME_SIZE_FEATURE_REPORT_LEN] = {0};
	g_autoptr(FuStructPixartTpTfReadCmd) st_read = fu_struct_pixart_tp_tf_read_cmd_new();
	g_autoptr(FuStructPixartTpTfReplyHdr) st_hdr = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* datalen = input length + 2 bytes reply length (low/high) */
	fu_struct_pixart_tp_tf_read_cmd_set_addr(st_read, addr);
	fu_struct_pixart_tp_tf_read_cmd_set_datalen(st_read, (guint16)in_bufsz + sizeof(guint16));
	fu_struct_pixart_tp_tf_read_cmd_set_reply_len(st_read, reply_len);

	/* append payload (optional) */
	if (in_bufsz > 0)
		g_byte_array_append(st_read->buf, in_buf, in_bufsz);

	/* append crc + tail */
	fu_byte_array_append_uint8(
	    st_read->buf,
	    fu_crc8(FU_CRC_KIND_B8_STANDARD, st_read->buf->data + 2, st_read->buf->len - 2));
	fu_byte_array_append_uint8(st_read->buf, FU_PIXART_TP_TF_FRAME_CONST_TAIL);
	fu_byte_array_set_size(st_read->buf, FU_PIXART_TP_TF_FRAME_SIZE_FEATURE_REPORT_LEN, 0x00);

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return NULL;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(proxy),
					  st_read->buf->data,
					  st_read->buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return NULL;

	fu_device_sleep(FU_DEVICE(self), 10);

	/* copy header to preserve emulation compat */
	if (!fu_memcpy_safe(io_buf,
			    sizeof(io_buf),
			    0,
			    st_read->buf->data,
			    st_read->buf->len,
			    0,
			    st_read->buf->len,
			    error))
		return NULL;
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(proxy),
					  io_buf,
					  sizeof(io_buf),
					  FU_IOCTL_FLAG_NONE,
					  error))
		return NULL;

	/* parse reply header */
	st_hdr = fu_struct_pixart_tp_tf_reply_hdr_parse(io_buf, sizeof(io_buf), 0, error);
	if (st_hdr == NULL)
		return NULL;

	if (fu_struct_pixart_tp_tf_reply_hdr_get_preamble(st_hdr) !=
		FU_PIXART_TP_TF_FRAME_CONST_PREAMBLE ||
	    fu_struct_pixart_tp_tf_reply_hdr_get_target_addr(st_hdr) !=
		FU_PIXART_TP_TF_TARGET_ADDR_RMI_FRAME) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "invalid header 0x%02x 0x%02x",
			    fu_struct_pixart_tp_tf_reply_hdr_get_preamble(st_hdr),
			    fu_struct_pixart_tp_tf_reply_hdr_get_target_addr(st_hdr));
		return NULL;
	}

	/* exception frame? */
	if ((fu_struct_pixart_tp_tf_reply_hdr_get_func(st_hdr) &
	     FU_PIXART_TP_TF_FRAME_CONST_EXCEPTION_FLAG) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "device returned exception 0x%02x",
			    fu_struct_pixart_tp_tf_reply_hdr_get_func(st_hdr));
		return NULL;
	}

	datalen = fu_struct_pixart_tp_tf_reply_hdr_get_datalen(st_hdr);
	if (datalen > sizeof(io_buf) - FU_STRUCT_PIXART_TP_TF_REPLY_HDR_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "frame exceeds feature report size");
		return NULL;
	}

	/* validate crc + tail */
	if (fu_crc8(FU_CRC_KIND_B8_STANDARD, io_buf + 2, datalen + 4) != io_buf[datalen + 6]) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "crc mismatch");
		return NULL;
	}
	if (io_buf[datalen + 7] != FU_PIXART_TP_TF_FRAME_CONST_TAIL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "tail mismatch");
		return NULL;
	}

	/* success */
	if (!fu_byte_array_append_safe(buf,
				       io_buf,
				       sizeof(io_buf),
				       FU_STRUCT_PIXART_TP_TF_REPLY_HDR_SIZE, /* offset */
				       datalen,
				       error))
		return NULL;
	return g_steal_pointer(&buf);
}

static gboolean
fu_pixart_tp_haptic_device_tf_read_firmware_version_cb(FuDevice *device,
						       gpointer user_data,
						       GError **error)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	FuPixartTpTfReadFwVersionCtx *ctx = (FuPixartTpTfReadFwVersionCtx *)user_data;
	g_autoptr(FuStructPixartTpTfVersionPayload) st = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_pixart_tp_haptic_device_tf_read_rmi(self,
						     FU_PIXART_TP_TF_CMD_READ_VERSION,
						     &ctx->mode,
						     1,
						     FU_STRUCT_PIXART_TP_TF_VERSION_PAYLOAD_SIZE,
						     error);
	if (buf == NULL)
		return FALSE;
	st = fu_struct_pixart_tp_tf_version_payload_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;

	ctx->major = fu_struct_pixart_tp_tf_version_payload_get_major(st);
	ctx->minor = fu_struct_pixart_tp_tf_version_payload_get_minor(st);
	ctx->patch = fu_struct_pixart_tp_tf_version_payload_get_patch(st);
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_tf_ensure_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	g_autoptr(FuStructPixartTpTfDownloadStatusPayload) st = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_pixart_tp_haptic_device_tf_read_rmi(
	    self,
	    FU_PIXART_TP_TF_CMD_READ_UPGRADE_STATUS,
	    NULL,
	    0,
	    FU_STRUCT_PIXART_TP_TF_DOWNLOAD_STATUS_PAYLOAD_SIZE,
	    error);
	if (buf == NULL)
		return FALSE;

	/* hdr + payload + trailer */
	st = fu_struct_pixart_tp_tf_download_status_payload_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* success */
	self->status = fu_struct_pixart_tp_tf_download_status_payload_get_status(st);
	self->packet_number = fu_struct_pixart_tp_tf_download_status_payload_get_packet_number(st);
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_tf_write_packet_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	FuPixartTpTfWritePacketCtx *ctx = (FuPixartTpTfWritePacketCtx *)user_data;

	return fu_pixart_tp_haptic_device_tf_write_rmi_with_packet(
	    self,
	    FU_PIXART_TP_TF_CMD_WRITE_UPGRADE_DATA,
	    ctx->packet_total,
	    ctx->packet_index,
	    ctx->chunk_data,
	    ctx->chunk_len,
	    error);
}

static gboolean
fu_pixart_tp_haptic_device_tf_read_firmware_version(FuPixartTpHapticDevice *self,
						    FuPixartTpTfFwMode mode,
						    guint8 *major,
						    guint8 *minor,
						    guint8 *patch,
						    GError **error)
{
	FuPixartTpTfReadFwVersionCtx ctx = {.mode = mode, .major = 0, .minor = 0, .patch = 0};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_haptic_device_tf_read_firmware_version_cb,
				  FU_PIXART_TP_TF_RETRY_COUNT,
				  (guint)FU_PIXART_TP_TF_RETRY_INTERVAL_MS,
				  &ctx,
				  error)) {
		g_prefix_error_literal(error, "failed to read firmware version: ");
		return FALSE;
	}

	/* success */
	if (major != NULL)
		*major = ctx.major;
	if (minor != NULL)
		*minor = ctx.minor;
	if (patch != NULL)
		*patch = ctx.patch;
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_tf_ensure_status(FuPixartTpHapticDevice *self, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_haptic_device_tf_ensure_status_cb,
				  FU_PIXART_TP_TF_RETRY_COUNT,
				  (guint)FU_PIXART_TP_TF_RETRY_INTERVAL_MS,
				  NULL,
				  error)) {
		g_prefix_error_literal(error, "failed to read download status: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_tf_exit_upgrade_mode(FuPixartTpHapticDevice *self, GError **error)
{
	return fu_pixart_tp_haptic_device_tf_write_rmi_cmd(self,
							   FU_PIXART_TP_TF_CMD_SET_UPGRADE_MODE,
							   FU_PIXART_TP_TF_UPGRADE_MODE_EXIT,
							   error);
}

static gboolean
fu_pixart_tp_haptic_device_tf_write_packet(FuPixartTpHapticDevice *self,
					   FuChunk *chk,
					   guint num_chunks,
					   guint32 retry_interval,
					   GError **error)
{
	FuPixartTpTfWritePacketCtx ctx = {
	    .packet_total = num_chunks,
	    .packet_index = fu_chunk_get_idx(chk) + 1,
	    .chunk_data = fu_chunk_get_data(chk),
	    .chunk_len = fu_chunk_get_data_sz(chk),
	};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_pixart_tp_haptic_device_tf_write_packet_cb,
				    FU_PIXART_TP_TF_RETRY_COUNT,
				    retry_interval,
				    &ctx,
				    error);
}

static gboolean
fu_pixart_tp_haptic_device_tf_write_packets(FuPixartTpHapticDevice *self,
					    FuChunkArray *chunks,
					    guint32 send_interval,
					    FuProgress *progress,
					    GError **error)
{
	guint num_chunks;

	/* sanity check */
	num_chunks = fu_chunk_array_length(chunks);
	if (num_chunks == 0) {
		g_debug("no firmware data to write");
		return TRUE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, num_chunks);
	for (guint i = 0; i < num_chunks; i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_pixart_tp_haptic_device_tf_write_packet(self,
								chk,
								num_chunks,
								(send_interval > 0) ? send_interval
										    : 50,
								error))
			return FALSE;

		if (send_interval > 0)
			fu_device_sleep(FU_DEVICE(self), send_interval);
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_tf_write_firmware(FuPixartTpHapticDevice *self,
					     guint32 send_interval,
					     GBytes *blob,
					     FuProgress *progress,
					     GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "disable-touch");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "enter-bootloader");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 9, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 86, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 0, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "exit-bootloader");

	/* disabling touch */
	if (!fu_pixart_tp_haptic_device_tf_write_rmi_cmd(self,
							 FU_PIXART_TP_TF_CMD_TOUCH_CONTROL,
							 FU_PIXART_TP_TF_TOUCH_CONTROL_DISABLE,
							 error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to disable touch");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* enter bootloader mode */
	if (!fu_pixart_tp_haptic_device_tf_write_rmi_cmd(self,
							 FU_PIXART_TP_TF_CMD_SET_UPGRADE_MODE,
							 FU_PIXART_TP_TF_UPGRADE_MODE_ENTER_BOOT,
							 error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to enter bootloader mode");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100);
	fu_progress_step_done(progress);

	/* erase flash */
	if (!fu_pixart_tp_haptic_device_tf_write_rmi_cmd(self,
							 FU_PIXART_TP_TF_CMD_SET_UPGRADE_MODE,
							 FU_PIXART_TP_TF_UPGRADE_MODE_ERASE_FLASH,
							 error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to send erase flash command");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 2000);
	fu_progress_step_done(progress);

	/* write packets */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       32);
	if (!fu_pixart_tp_haptic_device_tf_write_packets(self,
							 chunks,
							 send_interval,
							 fu_progress_get_child(progress),
							 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_pixart_tp_haptic_device_tf_ensure_status(self, error))
		return FALSE;
	if (self->status != 0 || self->packet_number != fu_chunk_array_length(chunks)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "upgrade failed, status=%u, device_packets=%u, expected_packets=%u",
			    self->status,
			    self->packet_number,
			    fu_chunk_array_length(chunks));
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 50);
	fu_progress_step_done(progress);

	/* exit upgrade mode */
	if (!fu_pixart_tp_haptic_device_tf_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");
	fu_device_sleep(FU_DEVICE(self), 1000);
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_tf_write_firmware_process(FuPixartTpHapticDevice *self,
						     guint32 send_interval,
						     GBytes *blob,
						     FuProgress *progress,
						     GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/*
	 * Workaround:
	 * Force the TP run mode to Force Run to prevent the TP from entering sleep during TF
	 * update, which can cause TF flashing to fail.
	 *
	 * Ideally, when the TP is switched to TF_UPDATE proxy mode it should stay awake. However,
	 * the current firmware cannot be changed, so we keep this as an AP-side workaround.
	 */
	if (!fu_pixart_tp_device_register_user_write(FU_PIXART_TP_DEVICE(proxy),
						     FU_PIXART_TP_USER_BANK_BANK0,
						     FU_PIXART_TP_REG_USER0_RUN_MODE,
						     FU_PIXART_TP_RUN_MODE_FORCE_RUN,
						     error)) {
		return FALSE;
	}

	if (!fu_pixart_tp_device_register_user_write(FU_PIXART_TP_DEVICE(proxy),
						     FU_PIXART_TP_USER_BANK_BANK0,
						     FU_PIXART_TP_REG_USER0_PROXY_MODE,
						     FU_PIXART_TP_PROXY_MODE_TF_UPDATE,
						     error))
		return FALSE;

	if (!fu_pixart_tp_haptic_device_tf_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	if (g_bytes_get_size(blob) < 128) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid firmware file: size too small for header check");
		return FALSE;
	}
	for (guint i = 6; i < 128; i++) {
		const guint8 *data = g_bytes_get_data(blob, NULL);
		if (data[i] != 0) {
			g_set_error_literal(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware file, non-zero data in header region");
			return FALSE;
		}
	}

	return fu_pixart_tp_haptic_device_tf_write_firmware(self,
							    send_interval,
							    blob,
							    progress,
							    error);
}

static gboolean
fu_pixart_tp_haptic_device_probe(FuDevice *device, GError **error)
{
	return fu_device_build_instance_id(device,
					   error,
					   "HIDRAW",
					   "VEN",
					   "DEV",
					   "COMPONENT",
					   NULL);
}

static gboolean
fu_pixart_tp_haptic_device_setup(FuDevice *device, GError **error)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	guint8 major = 0;
	guint8 minor = 0;
	guint8 patch = 0;
	g_autofree gchar *ver_str = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_version = NULL;

	/* exit TF upgrade/engineer mode (best-effort) */
	if (!fu_pixart_tp_haptic_device_tf_exit_upgrade_mode(self, &error_local)) {
		g_debug("haptic: ignoring failure to exit TF upgrade mode in setup: %s",
			error_local->message);
	}

	fu_device_sleep(FU_DEVICE(self), 1000);

	/* best-effort: if TF is not present or not responding, or respond error code, keep device
	 * online and need update */
	if (!fu_pixart_tp_haptic_device_tf_read_firmware_version(self,
								 FU_PIXART_TP_TF_FW_MODE_APP,
								 &major,
								 &minor,
								 &patch,
								 &error_version)) {
		g_debug("failed to read TF firmware version: %s", error_version->message);
		fu_device_set_version(device, "0.0.0");
		return TRUE;
	}

	/* if TF version is 255.x.x, flash is empty / bootloader state, needs update */
	if (major == 0xFF) {
		g_debug("TF in bootloader state (%u.%u.%u)", major, minor, patch);
		fu_device_set_version(device, "0.0.0");
		return TRUE;
	}

	/* success */
	ver_str = g_strdup_printf("%u.%u.%u", major, minor, patch);
	fu_device_set_version(device, ver_str);
	return TRUE;
}

static FuFirmware *
fu_pixart_tp_haptic_device_prepare_firmware(FuDevice *device,
					    GInputStream *stream,
					    FuProgress *progress,
					    FuFirmwareParseFlags flags,
					    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pixart_tp_firmware_new();

	/* parse the TP FWHD firmware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* find the TF_FORCE section image by ID */
	return fu_firmware_get_image_by_id(firmware, "tf-force", error);
}

static gboolean
fu_pixart_tp_haptic_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	guint32 send_interval = 0;
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(GByteArray) reserved = NULL;

	/* read send interval from reserved bytes */
	reserved = fu_pixart_tp_section_get_reserved(FU_PIXART_TP_SECTION(firmware));
	if (reserved == NULL || reserved->len < 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "reserved bytes too short for TF_FORCE section");
		return FALSE;
	}

	/* read TF payload */
	payload = fu_firmware_get_bytes(firmware, error);
	if (payload == NULL)
		return FALSE;
	if (g_bytes_get_size(payload) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty TF_FORCE payload");
		return FALSE;
	}

	/* call TF updater */
	send_interval = (guint32)reserved->data[3]; /* ms */
	if (!fu_pixart_tp_haptic_device_tf_write_firmware_process(self,
								  send_interval,
								  payload,
								  progress,
								  error)) {
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (fu_device_has_flag(proxy, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot update TF while TP parent is in bootloader mode; "
				    "please replug the device or update the TP firmware first");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_pixart_tp_haptic_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 4, "reload");
}

static gboolean
fu_pixart_tp_haptic_device_cleanup(FuDevice *device,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuPixartTpHapticDevice *self = FU_PIXART_TP_HAPTIC_DEVICE(device);
	FuDevice *proxy;
	g_autoptr(GError) error_local = NULL;

	/* exit TF upgrade/engineer mode (best-effort) */
	if (!fu_pixart_tp_haptic_device_tf_exit_upgrade_mode(self, &error_local)) {
		g_debug("ignoring failure to exit TF upgrade mode in cleanup: %s",
			error_local->message);
		g_clear_error(&error_local);
	}

	/* restore TP proxy mode back to normal (best-effort) */
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_pixart_tp_device_register_user_write(FU_PIXART_TP_DEVICE(proxy),
						     FU_PIXART_TP_USER_BANK_BANK0,
						     FU_PIXART_TP_REG_USER0_PROXY_MODE,
						     FU_PIXART_TP_PROXY_MODE_NORMAL,
						     &error_local)) {
		g_debug("ignoring failure to restore proxy mode in cleanup: %s",
			error_local->message);
		return TRUE;
	}

	/* restore the TP proxy run mode to normal */
	if (!fu_pixart_tp_device_register_user_write(FU_PIXART_TP_DEVICE(proxy),
						     FU_PIXART_TP_USER_BANK_BANK0,
						     FU_PIXART_TP_REG_USER0_RUN_MODE,
						     FU_PIXART_TP_RUN_MODE_AUTO,
						     &error_local)) {
		g_debug("ignoring failure to restore proxy run mode in cleanup: %s",
			error_local->message);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_haptic_device_reload(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best-effort: do not fail the whole update just because reload failed */
	if (!fu_pixart_tp_haptic_device_setup(device, &error_local))
		g_debug("failed to refresh tf firmware version: %s", error_local->message);

	/* success */
	return TRUE;
}

static void
fu_pixart_tp_haptic_device_class_init(FuPixartTpHapticDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_pixart_tp_haptic_device_to_string;
	device_class->probe = fu_pixart_tp_haptic_device_probe;
	device_class->setup = fu_pixart_tp_haptic_device_setup;
	device_class->prepare_firmware = fu_pixart_tp_haptic_device_prepare_firmware;
	device_class->write_firmware = fu_pixart_tp_haptic_device_write_firmware;
	device_class->detach = fu_pixart_tp_haptic_device_detach;
	device_class->set_progress = fu_pixart_tp_haptic_device_set_progress;
	device_class->cleanup = fu_pixart_tp_haptic_device_cleanup;
	device_class->reload = fu_pixart_tp_haptic_device_reload;
}

static void
fu_pixart_tp_haptic_device_init(FuPixartTpHapticDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.tp.haptic");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_instance_str(FU_DEVICE(self), "COMPONENT", "tf");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_logical_id(FU_DEVICE(self), "tf");
	fu_device_set_name(FU_DEVICE(self), "Touchpad Haptic");
	fu_device_set_summary(FU_DEVICE(self), "Force/haptic controller for touchpad");
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_PIXART_TP_DEVICE);
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
}

FuPixartTpHapticDevice *
fu_pixart_tp_haptic_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_PIXART_TP_HAPTIC_DEVICE, "proxy", proxy, NULL);
}
