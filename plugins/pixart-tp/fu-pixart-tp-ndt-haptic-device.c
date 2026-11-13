/* Copyright 2026 Harris Tai <harris_tai@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-device.h"
#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-ndt-haptic-device.h"
#include "fu-pixart-tp-ndt-struct.h"
#include "fu-pixart-tp-section.h"
#include "fu-pixart-tp-struct.h"

struct _FuPixartTpNdtHapticDevice {
	FuDevice parent_instance;
	FuPixartTpNdtStatus status;
};

G_DEFINE_TYPE(FuPixartTpNdtHapticDevice, fu_pixart_tp_ndt_haptic_device, FU_TYPE_DEVICE)

#define FU_PIXART_TP_NDT_VERSION_RETRY_COUNT	   3
#define FU_PIXART_TP_NDT_VERSION_RETRY_INTERVAL_MS 10
#define FU_PIXART_TP_NDT_READY_TIMEOUT_MS	   15000
#define FU_PIXART_TP_NDT_READY_POLL_INTERVAL_MS	   10

/* the TP firmware has to query the NDT before the version reply is ready */
#define FU_PIXART_TP_NDT_VERSION_REPLY_DELAY_MS 100

/* fu_pixart_tp_device_register_user_read() itself takes 10ms per read */
#define FU_PIXART_TP_NDT_REG_READ_DURATION_MS 10

/* layout of the NdtUpdate register: bit 0 READY, bit 1 START, bits 7:2 STATUS */
#define FU_PIXART_TP_NDT_STATUS_SHIFT 2

static const gchar *
fu_pixart_tp_ndt_haptic_device_status_to_string(FuPixartTpNdtStatus status)
{
	/* STATUS has six bits, but only some of the codes are named */
	const gchar *str = fu_pixart_tp_ndt_status_to_string(status);
	return str != NULL ? str : "unknown";
}

static void
fu_pixart_tp_ndt_haptic_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPixartTpNdtHapticDevice *self = FU_PIXART_TP_NDT_HAPTIC_DEVICE(device);
	g_autofree gchar *status =
	    g_strdup_printf("0x%02x [%s]",
			    (guint)self->status,
			    fu_pixart_tp_ndt_haptic_device_status_to_string(self->status));
	fwupd_codec_string_append(str, idt, "Status", status);
}

static FuPixartTpNdtStatus
fu_pixart_tp_ndt_haptic_device_get_status_bits(guint8 update_status)
{
	/* the session STATUS code is the top six bits of the 8-bit NdtUpdate register */
	return (FuPixartTpNdtStatus)(update_status >> FU_PIXART_TP_NDT_STATUS_SHIFT);
}

static gboolean
fu_pixart_tp_ndt_haptic_device_read_update_status(FuPixartTpNdtHapticDevice *self,
						  guint8 *update_status,
						  GError **error)
{
	FuDevice *proxy;

	/* the whole NdtUpdate register: READY, START and the session STATUS code */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	return fu_pixart_tp_device_register_user_read(FU_PIXART_TP_DEVICE(proxy),
						      FU_PIXART_TP_USER_BANK_BANK0,
						      FU_PIXART_TP_REG_USER0_NDT_UPDATE,
						      update_status,
						      error);
}

static gboolean
fu_pixart_tp_ndt_haptic_device_clear_ready_bit(FuPixartTpNdtHapticDevice *self, GError **error)
{
	FuDevice *proxy;
	guint8 update_status = 0;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* read-modify-write so that START and STATUS are left untouched */
	if (!fu_pixart_tp_ndt_haptic_device_read_update_status(self, &update_status, error))
		return FALSE;
	update_status &= (guint8)~FU_PIXART_TP_NDT_UPDATE_BIT_READY;
	return fu_pixart_tp_device_register_user_write(FU_PIXART_TP_DEVICE(proxy),
						       FU_PIXART_TP_USER_BANK_BANK0,
						       FU_PIXART_TP_REG_USER0_NDT_UPDATE,
						       update_status,
						       error);
}

static gboolean
fu_pixart_tp_ndt_haptic_device_start_session(FuPixartTpNdtHapticDevice *self, GError **error)
{
	FuDevice *proxy;
	guint8 update_status = 0;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* clear a READY left over from the previous session in the same write as START, otherwise
	 * the first wait would return before the chip ID check and the erase are done */
	if (!fu_pixart_tp_ndt_haptic_device_read_update_status(self, &update_status, error))
		return FALSE;
	update_status |= FU_PIXART_TP_NDT_UPDATE_BIT_START;
	update_status &= (guint8)~FU_PIXART_TP_NDT_UPDATE_BIT_READY;
	return fu_pixart_tp_device_register_user_write(FU_PIXART_TP_DEVICE(proxy),
						       FU_PIXART_TP_USER_BANK_BANK0,
						       FU_PIXART_TP_REG_USER0_NDT_UPDATE,
						       update_status,
						       error);
}

typedef struct {
	gboolean aborted;
	FuPixartTpNdtStatus status;
} FuPixartTpNdtWaitReadyCtx;

static gboolean
fu_pixart_tp_ndt_haptic_device_wait_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpNdtWaitReadyCtx *ctx = (FuPixartTpNdtWaitReadyCtx *)user_data;
	guint8 update_status = 0;
	FuPixartTpNdtStatus status;

	if (!fu_pixart_tp_ndt_haptic_device_read_update_status(
		FU_PIXART_TP_NDT_HAPTIC_DEVICE(device),
		&update_status,
		error))
		return FALSE;
	if ((update_status & FU_PIXART_TP_NDT_UPDATE_BIT_READY) != 0)
		return TRUE;

	/* the firmware clears START when it aborts the session, so do not wait for the timeout */
	status = fu_pixart_tp_ndt_haptic_device_get_status_bits(update_status);
	if ((update_status & FU_PIXART_TP_NDT_UPDATE_BIT_START) == 0 &&
	    status != FU_PIXART_TP_NDT_STATUS_OK) {
		ctx->aborted = TRUE;
		ctx->status = status;
		return TRUE;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "NDT is not ready");
	return FALSE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_wait_ready(FuPixartTpNdtHapticDevice *self, GError **error)
{
	FuPixartTpNdtWaitReadyCtx ctx = {0};

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_ndt_haptic_device_wait_ready_cb,
				  FU_PIXART_TP_NDT_READY_TIMEOUT_MS /
				      (FU_PIXART_TP_NDT_READY_POLL_INTERVAL_MS +
				       FU_PIXART_TP_NDT_REG_READ_DURATION_MS),
				  FU_PIXART_TP_NDT_READY_POLL_INTERVAL_MS,
				  &ctx,
				  error))
		return FALSE;
	if (ctx.aborted) {
		self->status = ctx.status;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "NDT session aborted with status 0x%02x [%s]",
			    (guint)ctx.status,
			    fu_pixart_tp_ndt_haptic_device_status_to_string(ctx.status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_ensure_session_idle(FuPixartTpNdtHapticDevice *self, GError **error)
{
	guint8 update_status = 0;

	/* only START matters here; STATUS is left over from the previous session */
	if (!fu_pixart_tp_ndt_haptic_device_read_update_status(self, &update_status, error))
		return FALSE;
	if ((update_status & FU_PIXART_TP_NDT_UPDATE_BIT_START) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "NDT update session already active");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_ensure_session_ok(FuPixartTpNdtHapticDevice *self, GError **error)
{
	guint8 update_status = 0;

	/* the STATUS code is the result of the session that has just finished */
	if (!fu_pixart_tp_ndt_haptic_device_read_update_status(self, &update_status, error))
		return FALSE;
	self->status = fu_pixart_tp_ndt_haptic_device_get_status_bits(update_status);
	if (self->status != FU_PIXART_TP_NDT_STATUS_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "NDT update failed with status 0x%02x [%s]",
			    (guint)self->status,
			    fu_pixart_tp_ndt_haptic_device_status_to_string(self->status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_write_chunk(FuPixartTpNdtHapticDevice *self,
					   FuChunk *chk,
					   gboolean is_last,
					   GError **error)
{
	FuDevice *proxy;
	g_autoptr(FuStructPixartTpNdtUpdateReport) st = fu_struct_pixart_tp_ndt_update_report_new();

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* the report is always the full size, only len says how much of data is valid */
	fu_struct_pixart_tp_ndt_update_report_set_len(st, (guint8)fu_chunk_get_data_sz(chk));
	fu_struct_pixart_tp_ndt_update_report_set_is_last(st,
							  is_last ? FU_PIXART_TP_NDT_IS_LAST_YES
								  : FU_PIXART_TP_NDT_IS_LAST_NO);
	if (!fu_struct_pixart_tp_ndt_update_report_set_data(st,
							    fu_chunk_get_data(chk),
							    fu_chunk_get_data_sz(chk),
							    error))
		return FALSE;

	/* not retried, as the firmware would append a resent chunk twice */
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(proxy),
					    st->buf->data,
					    st->buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pixart_tp_ndt_haptic_device_read_version_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint32 *version = (guint32 *)user_data;
	FuDevice *proxy;
	g_autoptr(FuStructPixartTpNdtVersionReq) st_req = fu_struct_pixart_tp_ndt_version_req_new();
	g_autoptr(FuStructPixartTpNdtVersionReply) st_res =
	    fu_struct_pixart_tp_ndt_version_reply_new();

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(proxy),
					  st_req->buf->data,
					  st_req->buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	fu_device_sleep(device, FU_PIXART_TP_NDT_VERSION_REPLY_DELAY_MS);
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(proxy),
					  st_res->buf->data,
					  st_res->buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	if (!fu_struct_pixart_tp_ndt_version_reply_validate(st_res->buf->data,
							    st_res->buf->len,
							    0x0,
							    error))
		return FALSE;

	/* success */
	*version = fu_struct_pixart_tp_ndt_version_reply_get_version(st_res);
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_ensure_version(FuPixartTpNdtHapticDevice *self, GError **error)
{
	guint32 version = 0;

	/* the version cannot be read while an update session is active */
	if (!fu_pixart_tp_ndt_haptic_device_ensure_session_idle(self, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_ndt_haptic_device_read_version_cb,
				  FU_PIXART_TP_NDT_VERSION_RETRY_COUNT,
				  FU_PIXART_TP_NDT_VERSION_RETRY_INTERVAL_MS,
				  &version,
				  error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_validate_image(GBytes *blob, GError **error)
{
	gsize bufsz = g_bytes_get_size(blob);
	guint32 valid_data_len;
	g_autoptr(FuStructPixartTpNdtImageHeader) st = NULL;

	/* this also fails if the header is truncated */
	st = fu_struct_pixart_tp_ndt_image_header_parse_bytes(blob, 0x0, error);
	if (st == NULL)
		return FALSE;

	valid_data_len = fu_struct_pixart_tp_ndt_image_header_get_valid_data_len(st);
	if (valid_data_len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "NDT image valid_data_len is zero");
		return FALSE;
	}
	if (bufsz != FU_STRUCT_PIXART_TP_NDT_IMAGE_HEADER_SIZE + valid_data_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "NDT image size invalid, got 0x%zx and expected 0x%x",
			    bufsz,
			    (guint)(FU_STRUCT_PIXART_TP_NDT_IMAGE_HEADER_SIZE + valid_data_len));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_probe(FuDevice *device, GError **error)
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
fu_pixart_tp_ndt_haptic_device_setup(FuDevice *device, GError **error)
{
	FuPixartTpNdtHapticDevice *self = FU_PIXART_TP_NDT_HAPTIC_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* an unreadable NDT is shown as 0.0.0.0 so it can still be recovered by an update,
	 * which matches what the firmware replies when the NDT image is corrupt */
	if (!fu_pixart_tp_ndt_haptic_device_ensure_version(self, &error_local)) {
		g_debug("failed to read NDT version, using 0.0.0.0: %s", error_local->message);
		fu_device_set_version_raw(device, 0);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_reload(FuDevice *device, GError **error)
{
	FuPixartTpNdtHapticDevice *self = FU_PIXART_TP_NDT_HAPTIC_DEVICE(device);

	/* the session has just reported success, so failing to read the version is a real error */
	return fu_pixart_tp_ndt_haptic_device_ensure_version(self, error);
}

static gchar *
fu_pixart_tp_ndt_haptic_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32((guint32)version_raw, fu_device_get_version_format(device));
}

static FuFirmware *
fu_pixart_tp_ndt_haptic_device_prepare_firmware(FuDevice *device,
						FuInputStream *stream,
						FuProgress *progress,
						FuFirmwareParseFlags flags,
						GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pixart_tp_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* the NDT image is one section of the TP firmware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	img = fu_firmware_get_image_by_idx(firmware, FU_PIXART_TP_UPDATE_TYPE_NDT_FORCE, error);
	if (img == NULL)
		return NULL;

	/* an NDT section that is not valid means the package is built wrong */
	if (!fu_pixart_tp_section_has_flag(FU_PIXART_TP_SECTION(img),
					   FU_PIXART_TP_FIRMWARE_FLAG_VALID)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "NDT section marked invalid");
		return NULL;
	}

	/* reject a bad header here so that it never reaches the device */
	blob = fu_firmware_get_bytes(img, error);
	if (blob == NULL)
		return NULL;
	if (!fu_pixart_tp_ndt_haptic_device_validate_image(blob, error))
		return NULL;

	/* success */
	return g_steal_pointer(&img);
}

static gboolean
fu_pixart_tp_ndt_haptic_device_write_chunks(FuPixartTpNdtHapticDevice *self,
					    FuChunkArray *chunks,
					    FuProgress *progress,
					    GError **error)
{
	guint chunks_len = fu_chunk_array_length(chunks);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks_len);

	for (guint i = 0; i < chunks_len; i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* the firmware sets READY again once it has consumed the chunk */
		if (!fu_pixart_tp_ndt_haptic_device_clear_ready_bit(self, error)) {
			g_prefix_error(error, "failed to clear READY for chunk 0x%x: ", i);
			return FALSE;
		}
		if (!fu_pixart_tp_ndt_haptic_device_write_chunk(self,
								chk,
								i == chunks_len - 1,
								error)) {
			g_prefix_error(error, "failed to write chunk 0x%x: ", i);
			return FALSE;
		}
		if (!fu_pixart_tp_ndt_haptic_device_wait_ready(self, error)) {
			g_prefix_error(error, "failed to wait for chunk 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuPixartTpNdtHapticDevice *self = FU_PIXART_TP_NDT_HAPTIC_DEVICE(device);
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "ensure-idle");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "start-session");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 0, "ensure-session-ok");

	/* the header is sent as part of the first chunk */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	chunks =
	    fu_chunk_array_new_from_bytes(blob,
					  FU_CHUNK_ADDR_OFFSET_NONE,
					  FU_CHUNK_PAGESZ_NONE,
					  FU_STRUCT_PIXART_TP_NDT_UPDATE_REPORT_N_ELEMENTS_DATA,
					  error);
	if (chunks == NULL)
		return FALSE;

	/* refuse to interrupt a session another client may have started */
	if (!fu_pixart_tp_ndt_haptic_device_ensure_session_idle(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the firmware checks the chip ID before the first READY, and erases as it writes */
	if (!fu_pixart_tp_ndt_haptic_device_start_session(self, error)) {
		g_prefix_error_literal(error, "failed to start session: ");
		return FALSE;
	}
	if (!fu_pixart_tp_ndt_haptic_device_wait_ready(self, error)) {
		g_prefix_error_literal(error, "failed to wait for session start: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write */
	if (!fu_pixart_tp_ndt_haptic_device_write_chunks(self,
							 chunks,
							 fu_progress_get_child(progress),
							 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the firmware clears START itself when the session ends */
	if (!fu_pixart_tp_ndt_haptic_device_ensure_session_ok(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_ndt_haptic_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;

	/* the NDT is only reachable through the TP runtime firmware */
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (fu_device_has_flag(proxy, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot update the NDT while the TP is in bootloader mode, "
				    "replug the device or update the TP firmware first");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_pixart_tp_ndt_haptic_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_pixart_tp_ndt_haptic_device_init(FuPixartTpNdtHapticDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.tp.ndt.haptic");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_instance_str(FU_DEVICE(self), "COMPONENT", "ndt");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_logical_id(FU_DEVICE(self), "ndt");
	fu_device_set_name(FU_DEVICE(self), "Haptic");
	fu_device_set_summary(FU_DEVICE(self), "Force/haptic controller for touchpad");
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_PIXART_TP_DEVICE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_PIXART_TP_SECTION);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
}

static void
fu_pixart_tp_ndt_haptic_device_class_init(FuPixartTpNdtHapticDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_pixart_tp_ndt_haptic_device_to_string;
	device_class->probe = fu_pixart_tp_ndt_haptic_device_probe;
	device_class->setup = fu_pixart_tp_ndt_haptic_device_setup;
	device_class->reload = fu_pixart_tp_ndt_haptic_device_reload;
	device_class->prepare_firmware = fu_pixart_tp_ndt_haptic_device_prepare_firmware;
	device_class->write_firmware = fu_pixart_tp_ndt_haptic_device_write_firmware;
	device_class->detach = fu_pixart_tp_ndt_haptic_device_detach;
	device_class->set_progress = fu_pixart_tp_ndt_haptic_device_set_progress;
	device_class->convert_version = fu_pixart_tp_ndt_haptic_device_convert_version;
}

FuPixartTpNdtHapticDevice *
fu_pixart_tp_ndt_haptic_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_PIXART_TP_NDT_HAPTIC_DEVICE, "proxy", proxy, NULL);
}
