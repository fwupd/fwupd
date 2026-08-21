/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-device.h"
#include "fu-focal-moc-struct.h"

typedef struct {
	guint8 ep_in;
	guint8 ep_out;
	guint8 iface;
	FuFocalMocIapStatusLayout iap_status_layout;
} FuFocalMocDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFocalMocDevice, fu_focal_moc_device, FU_TYPE_USB_DEVICE)
#define GET_PRIVATE(o) (fu_focal_moc_device_get_instance_private(o))

#define FU_FOCAL_MOC_USB_INTERFACE	    0
#define FU_FOCAL_MOC_USB_TIMEOUT_MS	    5000
#define FU_FOCAL_MOC_FRAME_TIMEOUT_MS	    8000
#define FU_FOCAL_MOC_COMMIT_TIMEOUT_MS	    15000
#define FU_FOCAL_MOC_RESPONSE_SIZE	    2048
#define FU_FOCAL_MOC_MAX_FIRMWARE_SIZE	    (G_MAXUINT8 * FU_FOCAL_MOC_YMODEM_DATA_SIZE)
#define FU_FOCAL_MOC_FIFO_CLEAR_RETRIES	    50
#define FU_FOCAL_MOC_FIFO_CLEAR_CONSECUTIVE 3
#define FU_FOCAL_MOC_FRAME_RETRIES	    3
#define FU_FOCAL_MOC_INSTALL_DURATION	    15

static void
fu_focal_moc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);

	fwupd_codec_string_append_hex(str, idt, "Interface", priv->iface);
	fwupd_codec_string_append_hex(str, idt, "EndpointIn", priv->ep_in);
	fwupd_codec_string_append_hex(str, idt, "EndpointOut", priv->ep_out);
	fwupd_codec_string_append(
	    str,
	    idt,
	    "IapStatusLayout",
	    fu_focal_moc_iap_status_layout_to_string(priv->iap_status_layout));
}

void
fu_focal_moc_device_set_iap_status_layout(FuFocalMocDevice *self,
					  FuFocalMocIapStatusLayout iap_status_layout)
{
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_FOCAL_MOC_DEVICE(self));
	priv->iap_status_layout = iap_status_layout;
}

gboolean
fu_focal_moc_device_send_raw(FuFocalMocDevice *self,
			     const guint8 *buf,
			     gsize bufsz,
			     guint timeout_ms,
			     GError **error)
{
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);
	gsize actual = 0;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_DEVICE(self), FALSE);

	fu_dump_full(G_LOG_DOMAIN, "request", buf, bufsz, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 priv->ep_out,
					 (guint8 *)buf,
					 bufsz,
					 &actual,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to send command: ");
		return FALSE;
	}
	if (actual != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "short USB write: actual=0x%zx expected=0x%zx",
			    actual,
			    bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_send(FuFocalMocDevice *self, GByteArray *buf, guint timeout_ms, GError **error)
{
	return fu_focal_moc_device_send_raw(self, buf->data, buf->len, timeout_ms, error);
}

GByteArray *
fu_focal_moc_device_receive_raw(FuFocalMocDevice *self, guint timeout_ms, GError **error)
{
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);
	gsize actual = 0;
	guint8 buf[FU_FOCAL_MOC_RESPONSE_SIZE] = {0};
	g_autoptr(GByteArray) data = g_byte_array_new();

	g_return_val_if_fail(FU_IS_FOCAL_MOC_DEVICE(self), NULL);

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 priv->ep_in,
					 buf,
					 sizeof(buf),
					 &actual,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "failed to receive response: ");
		return NULL;
	}
	fu_dump_full(G_LOG_DOMAIN, "response", buf, actual, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);
	g_byte_array_append(data, buf, actual);

	/* success */
	return g_steal_pointer(&data);
}

static GByteArray *
fu_focal_moc_device_receive(FuFocalMocDevice *self,
			    guint timeout_ms,
			    guint8 *status,
			    GError **error)
{
	g_autoptr(GByteArray) response = NULL;

	response = fu_focal_moc_device_receive_raw(self, timeout_ms, error);
	if (response == NULL)
		return NULL;
	return fu_focal_moc_packet_parse(
	    response->data,
	    response->len,
	    fu_device_has_private_flag(FU_DEVICE(self), FU_FOCAL_MOC_DEVICE_FLAG_LEGACY_TRAILER),
	    status,
	    error);
}

static GByteArray *
fu_focal_moc_device_command_plaintext(FuFocalMocDevice *self,
				      guint8 command,
				      const guint8 *payload,
				      gsize payload_sz,
				      guint timeout_ms,
				      guint8 *status,
				      GError **error)
{
	g_autoptr(GByteArray) request = NULL;

	request = fu_focal_moc_packet_new(command, payload, payload_sz, error);
	if (request == NULL)
		return NULL;
	if (!fu_focal_moc_device_send(self, request, timeout_ms, error))
		return NULL;
	return fu_focal_moc_device_receive(self, timeout_ms, status, error);
}

static GByteArray *
fu_focal_moc_device_command(FuFocalMocDevice *self,
			    guint8 command,
			    const guint8 *payload,
			    gsize payload_sz,
			    guint timeout_ms,
			    guint8 *status,
			    GError **error)
{
	FuFocalMocDeviceClass *klass = FU_FOCAL_MOC_DEVICE_GET_CLASS(self);
	return klass->command(self, command, payload, payload_sz, timeout_ms, status, error);
}

static gboolean
fu_focal_moc_device_status_error(FuFocalMocDevice *self,
				 guint8 status,
				 const gchar *context,
				 FwupdError invalid_command_code,
				 GByteArray *data,
				 GError **error)
{
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);
	gboolean status_aligned = TRUE;

	/* only a bootloader that failed the capability probe uses the legacy
	 * layout; the runtime firmware always uses the unified one */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	    priv->iap_status_layout == FU_FOCAL_MOC_IAP_STATUS_LAYOUT_LEGACY)
		status_aligned = FALSE;
	return fu_focal_moc_status_to_error(status,
					    context,
					    invalid_command_code,
					    status_aligned,
					    data,
					    error);
}

static GByteArray *
fu_focal_moc_device_command_ok(FuFocalMocDevice *self,
			       FuFocalMocCmd command,
			       const guint8 *payload,
			       gsize payload_sz,
			       guint timeout_ms,
			       const gchar *context,
			       GError **error)
{
	guint8 status = 0;
	g_autoptr(GByteArray) data = NULL;

	data = fu_focal_moc_device_command(self,
					   command,
					   payload,
					   payload_sz,
					   timeout_ms,
					   &status,
					   error);
	if (data == NULL)
		return NULL;
	if (status != FU_FOCAL_MOC_STATUS_OK) {
		fu_focal_moc_device_status_error(self,
						 status,
						 context,
						 FWUPD_ERROR_NOT_SUPPORTED,
						 data,
						 error);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&data);
}

static gboolean
fu_focal_moc_device_probe_alive(FuFocalMocDevice *self, guint timeout_ms, GError **error)
{
	g_autoptr(FuStructFocalMocWakeUp) st = NULL;
	g_autoptr(GByteArray) data = NULL;

	data = fu_focal_moc_device_command_ok(self,
					      FU_FOCAL_MOC_CMD_WAKE_UP,
					      NULL,
					      0,
					      timeout_ms,
					      "wake-up rejected",
					      error);
	if (data == NULL)
		return FALSE;
	/* an ACK with no payload leaves data->data NULL, which the parser rejects
	 * with a critical rather than a GError */
	if (data->len < FU_STRUCT_FOCAL_MOC_WAKE_UP_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wake-up response too small: 0x%x",
			    data->len);
		return FALSE;
	}
	st = fu_struct_focal_moc_wake_up_parse(data->data, data->len, 0x0, error);
	if (st == NULL) {
		g_prefix_error_literal(error, "wake-up response invalid: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	guint consecutive;
} FuFocalMocDeviceFifoHelper;

static gboolean
fu_focal_moc_device_fifo_clear_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	FuFocalMocDeviceFifoHelper *helper = user_data;
	g_autoptr(GError) error_local = NULL;

	if (!fu_focal_moc_device_probe_alive(self, 250, &error_local)) {
		helper->consecutive = 0;
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "FIFO did not clear: ");
		return FALSE;
	}
	/* a single response may be stale buffer, so require a quiet run */
	helper->consecutive++;
	if (helper->consecutive < FU_FOCAL_MOC_FIFO_CLEAR_CONSECUTIVE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "FIFO returned %u of %u consecutive wake-up responses",
			    helper->consecutive,
			    (guint)FU_FOCAL_MOC_FIFO_CLEAR_CONSECUTIVE);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_wait_fifo_clear(FuFocalMocDevice *self, GError **error)
{
	FuFocalMocDeviceFifoHelper helper = {0};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focal_moc_device_fifo_clear_cb,
				    FU_FOCAL_MOC_FIFO_CLEAR_RETRIES,
				    5, /* ms */
				    &helper,
				    error);
}

typedef struct {
	GByteArray *frame;
	GByteArray *data;
	guint attempts;
	guint8 status;
} FuFocalMocDeviceFrameHelper;

static gboolean
fu_focal_moc_device_send_frame_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	FuFocalMocDeviceFrameHelper *helper = user_data;
	g_autoptr(GByteArray) data = NULL;

	/* leftover frames from the failed attempt block the endpoint until they
	 * are drained */
	if (helper->attempts++ > 0) {
		if (!fu_focal_moc_device_wait_fifo_clear(self, error))
			return FALSE;
	}
	data = fu_focal_moc_device_command(self,
					   FU_FOCAL_MOC_CMD_SEND_FIRMWARE,
					   helper->frame->data,
					   helper->frame->len,
					   FU_FOCAL_MOC_FRAME_TIMEOUT_MS,
					   &helper->status,
					   error);
	if (data == NULL)
		return FALSE;
	helper->data = g_steal_pointer(&data);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_send_frame(FuFocalMocDevice *self,
			       GByteArray *frame,
			       guint16 sequence,
			       const gchar *kind,
			       GError **error)
{
	FuFocalMocDeviceFrameHelper helper = {.frame = frame};
	g_autoptr(GByteArray) data = NULL;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_focal_moc_device_send_frame_cb,
				  FU_FOCAL_MOC_FRAME_RETRIES,
				  5, /* ms */
				  &helper,
				  error)) {
		g_prefix_error(error, "%s frame failed: seq=%u: ", kind, sequence);
		return FALSE;
	}
	data = helper.data;
	if (helper.status != FU_FOCAL_MOC_STATUS_OK) {
		return fu_focal_moc_device_status_error(self,
							helper.status,
							kind,
							FWUPD_ERROR_WRITE,
							data,
							error);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_focal_moc_device_build_soh(FuFocalMocDevice *self,
			      gsize firmware_sz,
			      guint32 crc32,
			      GError **error)
{
	return fu_focal_moc_ymodem_build_soh(firmware_sz, crc32, error);
}

static GByteArray *
fu_focal_moc_device_build_data(FuFocalMocDevice *self,
			       FuFocalMocFrame kind,
			       guint16 sequence,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	return fu_focal_moc_ymodem_build_data(kind, sequence, buf, bufsz, error);
}

static FuInputStream *
fu_focal_moc_device_get_firmware_stream(FuFocalMocDevice *self,
					FuFirmware *firmware,
					GError **error)
{
	return fu_firmware_get_stream(firmware, error);
}

static gboolean
fu_focal_moc_device_write_soh(FuFocalMocDevice *self, FuInputStream *stream, GError **error)
{
	FuFocalMocDeviceClass *klass = FU_FOCAL_MOC_DEVICE_GET_CLASS(self);
	gsize streamsz = 0;
	/* only this seed makes fu_input_stream_compute_crc32() agree with fu_crc32() */
	guint32 crc32 = G_MAXUINT32;
	g_autoptr(GByteArray) frame = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &crc32, error))
		return FALSE;
	frame = klass->build_soh(self, streamsz, crc32, error);
	if (frame == NULL)
		return FALSE;

	/* success */
	return fu_focal_moc_device_send_frame(self, frame, 0, "SOH rejected", error);
}

static gboolean
fu_focal_moc_device_write_chunks(FuFocalMocDevice *self,
				 FuChunkArray *chunks,
				 FuProgress *progress,
				 GError **error)
{
	FuFocalMocDeviceClass *klass = FU_FOCAL_MOC_DEVICE_GET_CLASS(self);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		gboolean is_last = i + 1 == fu_chunk_array_length(chunks);
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) frame = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		frame = klass->build_data(self,
					  is_last ? FU_FOCAL_MOC_FRAME_EOT : FU_FOCAL_MOC_FRAME_STX,
					  (guint16)(i + 1),
					  fu_chunk_get_data(chk),
					  fu_chunk_get_data_sz(chk),
					  error);
		if (frame == NULL)
			return FALSE;
		if (!fu_focal_moc_device_send_frame(self,
						    frame,
						    (guint16)(i + 1),
						    is_last ? "EOT rejected" : "STX rejected",
						    error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_commit(FuFocalMocDevice *self, GError **error)
{
	guint8 mode = FU_FOCAL_MOC_BOOT_MODE_APP;
	guint8 status = 0;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GByteArray) request = NULL;
	g_autoptr(GError) error_local = NULL;

	request =
	    fu_focal_moc_packet_new(FU_FOCAL_MOC_CMD_SET_BOOT_MODE, &mode, sizeof(mode), error);
	if (request == NULL)
		return FALSE;
	if (!fu_focal_moc_device_send(self, request, FU_FOCAL_MOC_COMMIT_TIMEOUT_MS, error))
		return FALSE;
	data = fu_focal_moc_device_receive(self,
					   FU_FOCAL_MOC_COMMIT_TIMEOUT_MS,
					   &status,
					   &error_local);
	if (data == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("commit response unavailable after request: %s",
				error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to read commit response: ");
		return FALSE;
	}
	if (status != FU_FOCAL_MOC_STATUS_OK) {
		return fu_focal_moc_device_status_error(self,
							status,
							"commit rejected",
							FWUPD_ERROR_SIGNATURE_INVALID,
							data,
							error);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_ymodem(FuFocalMocDevice *self,
				 FuInputStream *stream,
				 FuProgress *progress,
				 GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	/* the last chunk is sent as EOT, so a payload that is an exact multiple of
	 * the frame size ends on a full frame rather than an empty one */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_FOCAL_MOC_YMODEM_DATA_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "soh");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, "commit");

	if (!fu_focal_moc_device_write_soh(self, stream, error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_focal_moc_device_write_chunks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_focal_moc_device_commit(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_ensure_bootloader_layout(FuFocalMocDevice *self, GError **error)
{
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);
	guint8 status = 0;
	g_autoptr(GByteArray) data = NULL;

	/* the raw status is required: a bootloader without the command replies
	 * INVALID_COMMAND, which fu_focal_moc_device_command_ok() would turn
	 * into a generic error */
	data = fu_focal_moc_device_command(self,
					   FU_FOCAL_MOC_CMD_GET_FP_VERSION,
					   NULL,
					   0,
					   FU_FOCAL_MOC_USB_TIMEOUT_MS,
					   &status,
					   error);
	if (data == NULL) {
		g_prefix_error_literal(error, "bootloader capability probe failed: ");
		return FALSE;
	}
	return fu_focal_moc_iap_status_layout_from_probe(status,
							 data->data,
							 data->len,
							 &priv->iap_status_layout,
							 error);
}

static gboolean
fu_focal_moc_device_ensure_version(FuFocalMocDevice *self, GError **error)
{
	FuFocalMocDeviceClass *klass = FU_FOCAL_MOC_DEVICE_GET_CLASS(self);
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);
	gboolean is_bootloader = FALSE;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) data = NULL;

	/* never reuse a layout probed on a previous enumeration */
	priv->iap_status_layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN;
	data = fu_focal_moc_device_command_ok(self,
					      FU_FOCAL_MOC_CMD_GET_FW_VERSION,
					      NULL,
					      0,
					      FU_FOCAL_MOC_USB_TIMEOUT_MS,
					      "version command rejected",
					      error);
	if (data == NULL)
		return FALSE;
	version = fu_focal_moc_version_parse(data->data, data->len, &is_bootloader, error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version(FU_DEVICE(self), version);
	if (is_bootloader)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	if (fu_focal_moc_iap_probe_required(is_bootloader)) {
		if (!klass->ensure_bootloader_layout(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_probe(FuDevice *device, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	FuFocalMocDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) interfaces = NULL;

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->probe(device, error))
		return FALSE;

	interfaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (interfaces == NULL)
		return FALSE;
	for (guint i = 0; i < interfaces->len; i++) {
		FuUsbInterface *iface = g_ptr_array_index(interfaces, i);
		g_autoptr(GPtrArray) endpoints = NULL;

		if (fu_usb_interface_get_number(iface) != FU_FOCAL_MOC_USB_INTERFACE)
			continue;
		endpoints = fu_usb_interface_get_endpoints(iface);
		for (guint j = 0; endpoints != NULL && j < endpoints->len; j++) {
			FuUsbEndpoint *endpoint = g_ptr_array_index(endpoints, j);
			if (fu_usb_endpoint_get_direction(endpoint) ==
			    FU_USB_DIRECTION_DEVICE_TO_HOST)
				priv->ep_in = fu_usb_endpoint_get_address(endpoint);
			else
				priv->ep_out = fu_usb_endpoint_get_address(endpoint);
		}
		priv->iface = fu_usb_interface_get_number(iface);
		break;
	}
	if (priv->ep_in == 0 || priv->ep_out == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "bulk endpoint pair not found on interface 0");
		return FALSE;
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), priv->iface);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_setup(FuDevice *device, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->setup(device, error))
		return FALSE;
	if (!fu_focal_moc_device_ensure_version(self, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	guint8 mode = FU_FOCAL_MOC_BOOT_MODE_IAP;
	g_autoptr(GByteArray) data = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	data = fu_focal_moc_device_command_ok(self,
					      FU_FOCAL_MOC_CMD_SET_BOOT_MODE,
					      &mode,
					      sizeof(mode),
					      FU_FOCAL_MOC_USB_TIMEOUT_MS,
					      "enter IAP rejected",
					      error);
	if (data == NULL)
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	FuFocalMocDeviceClass *klass = FU_FOCAL_MOC_DEVICE_GET_CLASS(self);
	g_autoptr(FuInputStream) stream = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "firmware write requires IAP mode");
		return FALSE;
	}
	stream = klass->get_firmware_stream(self, firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_focal_moc_device_probe_alive(self, FU_FOCAL_MOC_USB_TIMEOUT_MS, error)) {
		g_prefix_error_literal(error, "IAP communication probe failed: ");
		return FALSE;
	}
	if (!fu_focal_moc_device_write_ymodem(self, stream, progress, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static void
fu_focal_moc_device_set_progress(FuDevice *device, FuProgress *progress)
{
	/* the write step also waits for the runtime to re-enumerate, so attach and
	 * reload complete immediately */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 15, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_focal_moc_device_init(FuFocalMocDevice *self)
{
	fu_device_add_private_flag(FU_DEVICE(self), FU_FOCAL_MOC_DEVICE_FLAG_LEGACY_TRAILER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_FOCAL_MOC_MAX_FIRMWARE_SIZE);
	fu_device_set_install_duration(FU_DEVICE(self), FU_FOCAL_MOC_INSTALL_DURATION);
	fu_device_add_protocol(FU_DEVICE(self), "com.focal.moc");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-on-chip fingerprint sensor");
}

static void
fu_focal_moc_device_class_init(FuFocalMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_focal_moc_device_to_string;
	device_class->probe = fu_focal_moc_device_probe;
	device_class->setup = fu_focal_moc_device_setup;
	device_class->detach = fu_focal_moc_device_detach;
	device_class->write_firmware = fu_focal_moc_device_write_firmware;
	device_class->set_progress = fu_focal_moc_device_set_progress;
	klass->command = fu_focal_moc_device_command_plaintext;
	klass->build_soh = fu_focal_moc_device_build_soh;
	klass->build_data = fu_focal_moc_device_build_data;
	klass->get_firmware_stream = fu_focal_moc_device_get_firmware_stream;
	klass->ensure_bootloader_layout = fu_focal_moc_device_ensure_bootloader_layout;
	fu_device_register_private_flag(device_class, FU_FOCAL_MOC_DEVICE_FLAG_LEGACY_TRAILER);
}
