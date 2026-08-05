/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-device.h"
#include "fu-focal-moc-firmware.h"
#include "fu-focal-moc-struct.h"
#include "fu-focal-moc-transport.h"

struct _FuFocalMocDevice {
	FuUsbDevice parent_instance;
	guint8 ep_in;
	guint8 ep_out;
	guint8 iface;
	FuFocalMocIapStatusLayout iap_status_layout;
	FuFocalMocTransport *transport;
};

G_DEFINE_TYPE(FuFocalMocDevice, fu_focal_moc_device, FU_TYPE_USB_DEVICE)

#define FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V1 "update-protocol-v1"
#define FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V2 "update-protocol-v2"

#define FU_FOCAL_MOC_USB_PID_V4_IAP	    0x0201
#define FU_FOCAL_MOC_USB_INTERFACE	    0
#define FU_FOCAL_MOC_USB_TIMEOUT_MS	    5000
#define FU_FOCAL_MOC_FRAME_TIMEOUT_MS	    8000
#define FU_FOCAL_MOC_COMMIT_TIMEOUT_MS	    15000
#define FU_FOCAL_MOC_RESPONSE_SIZE	    2048
#define FU_FOCAL_MOC_V1_MAX_FIRMWARE_SIZE   (G_MAXUINT8 * FU_FOCAL_MOC_YMODEM_DATA_SIZE)
#define FU_FOCAL_MOC_V2_MAX_FIRMWARE_SIZE   ((384 * FU_KB) - FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE)
#define FU_FOCAL_MOC_FIFO_CLEAR_RETRIES	    500
#define FU_FOCAL_MOC_FIFO_CLEAR_CONSECUTIVE 3
#define FU_FOCAL_MOC_FRAME_RETRIES	    3
#define FU_FOCAL_MOC_V1_INSTALL_DURATION    15
#define FU_FOCAL_MOC_V2_INSTALL_DURATION    25

static gboolean
fu_focal_moc_device_is_protocol_v2(FuFocalMocDevice *self)
{
	return fu_device_has_private_flag(FU_DEVICE(self),
					  FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V2);
}

static gboolean
fu_focal_moc_device_is_v2_iap(FuFocalMocDevice *self)
{
	return fu_focal_moc_device_is_protocol_v2(self) &&
	       fu_device_get_pid(FU_DEVICE(self)) == FU_FOCAL_MOC_USB_PID_V4_IAP;
}

static guint
fu_focal_moc_device_protocol_version(FuFocalMocDevice *self)
{
	return fu_focal_moc_device_is_protocol_v2(self) ? 2 : 1;
}

static void
fu_focal_moc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);

	fwupd_codec_string_append_hex(str, idt, "Interface", self->iface);
	fwupd_codec_string_append_hex(str, idt, "EndpointIn", self->ep_in);
	fwupd_codec_string_append_hex(str, idt, "EndpointOut", self->ep_out);
	fwupd_codec_string_append(str,
				  idt,
				  "UpdateProtocol",
				  fu_focal_moc_device_is_protocol_v2(self) ? "v2" : "v1");
	fwupd_codec_string_append(
	    str,
	    idt,
	    "IapStatusLayout",
	    fu_focal_moc_iap_status_layout_to_string(self->iap_status_layout));
	if (fu_focal_moc_device_is_protocol_v2(self))
		fwupd_codec_string_append_bool(str,
					       idt,
					       "TransportActive",
					       fu_focal_moc_transport_is_active(self->transport));
}

static gboolean
fu_focal_moc_device_send_raw(gpointer user_data,
			     const guint8 *buf,
			     gsize bufsz,
			     guint timeout_ms,
			     GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(user_data);
	gsize actual = 0;

	fu_dump_full(G_LOG_DOMAIN, "request", buf, bufsz, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 self->ep_out,
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
			    "short USB write: actual=0x%x expected=0x%x",
			    (guint)actual,
			    (guint)bufsz);
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

static GByteArray *
fu_focal_moc_device_receive_raw(gpointer user_data, guint timeout_ms, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(user_data);
	gsize actual = 0;
	guint8 buf[FU_FOCAL_MOC_RESPONSE_SIZE] = {0};
	g_autoptr(GByteArray) data = g_byte_array_new();

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 self->ep_in,
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
	return fu_focal_moc_packet_parse(response->data,
					 response->len,
					 !fu_focal_moc_device_is_protocol_v2(self),
					 status,
					 error);
}

static GByteArray *
fu_focal_moc_device_command(FuFocalMocDevice *self,
			    FuFocalMocCmd command,
			    const guint8 *payload,
			    gsize payload_sz,
			    guint timeout_ms,
			    guint8 *status,
			    GError **error)
{
	g_autoptr(GByteArray) request = NULL;

	if (fu_focal_moc_device_is_protocol_v2(self) && !fu_focal_moc_device_is_v2_iap(self))
		return fu_focal_moc_transport_command(self->transport,
						      command,
						      payload,
						      payload_sz,
						      timeout_ms,
						      status,
						      error);
	request = fu_focal_moc_packet_new(command, payload, payload_sz, error);
	if (request == NULL)
		return NULL;
	if (!fu_focal_moc_device_send(self, request, timeout_ms, error))
		return NULL;
	return fu_focal_moc_device_receive(self, timeout_ms, status, error);
}

static gboolean
fu_focal_moc_device_status_error(FuFocalMocDevice *self,
				 guint8 status,
				 const gchar *context,
				 FwupdError invalid_command_code,
				 GByteArray *data,
				 GError **error)
{
	gboolean status_aligned = TRUE;

	/* only a protocol v1 bootloader that failed the capability probe uses the
	 * legacy layout; the runtime firmware and protocol v2 bootloaders always
	 * use the unified one */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	    !fu_focal_moc_device_is_protocol_v2(self) &&
	    self->iap_status_layout == FU_FOCAL_MOC_IAP_STATUS_LAYOUT_LEGACY)
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

static gboolean
fu_focal_moc_device_wait_fifo_clear(FuFocalMocDevice *self, GError **error)
{
	guint consecutive = 0;
	g_autoptr(GError) error_last = NULL;

	for (guint i = 0; i < FU_FOCAL_MOC_FIFO_CLEAR_RETRIES; i++) {
		g_autoptr(GError) error_local = NULL;

		if (fu_focal_moc_device_probe_alive(self, 250, &error_local)) {
			consecutive++;
			if (consecutive == FU_FOCAL_MOC_FIFO_CLEAR_CONSECUTIVE)
				return TRUE;
		} else {
			consecutive = 0;
			g_clear_error(&error_last);
			error_last = g_steal_pointer(&error_local);
		}
		fu_device_sleep(FU_DEVICE(self), 5);
	}
	if (error_last != NULL) {
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_last),
					   "FIFO did not clear: ");
		return FALSE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "FIFO did not return three consecutive wake-up responses");
	return FALSE;
}

static gboolean
fu_focal_moc_device_send_frame(FuFocalMocDevice *self,
			       GByteArray *frame,
			       guint16 sequence,
			       const gchar *kind,
			       GError **error)
{
	for (guint attempt = 0; attempt < FU_FOCAL_MOC_FRAME_RETRIES; attempt++) {
		guint8 status = 0;
		g_autoptr(GByteArray) data = NULL;
		g_autoptr(GError) error_local = NULL;

		data = fu_focal_moc_device_command(self,
						   FU_FOCAL_MOC_CMD_SEND_FIRMWARE,
						   frame->data,
						   frame->len,
						   FU_FOCAL_MOC_FRAME_TIMEOUT_MS,
						   &status,
						   &error_local);
		if (data != NULL) {
			if (status == FU_FOCAL_MOC_STATUS_OK)
				return TRUE;
			return fu_focal_moc_device_status_error(self,
								status,
								kind,
								FWUPD_ERROR_WRITE,
								data,
								error);
		}
		if (attempt + 1 == FU_FOCAL_MOC_FRAME_RETRIES) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "%s frame failed: seq=%u: ",
						   kind,
						   sequence);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 5);
		if (!fu_focal_moc_device_wait_fifo_clear(self, error))
			return FALSE;
	}
	g_assert_not_reached();
}

static gboolean
fu_focal_moc_device_write_soh(FuFocalMocDevice *self, FuInputStream *stream, GError **error)
{
	gsize streamsz = 0;
	/* only this seed makes fu_input_stream_compute_crc32() agree with fu_crc32() */
	guint32 crc32 = G_MAXUINT32;
	g_autoptr(GByteArray) frame = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &crc32, error))
		return FALSE;
	frame = fu_focal_moc_ymodem_build_soh(fu_focal_moc_device_protocol_version(self),
					      streamsz,
					      crc32,
					      error);
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
	guint protocol_version = fu_focal_moc_device_protocol_version(self);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		gboolean is_last = i + 1 == fu_chunk_array_length(chunks);
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) frame = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		frame = fu_focal_moc_ymodem_build_data(protocol_version,
						       is_last ? FU_FOCAL_MOC_FRAME_EOT
							       : FU_FOCAL_MOC_FRAME_STX,
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
	guint8 mode = 0;
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
	if (status != FU_FOCAL_MOC_STATUS_OK)
		return fu_focal_moc_device_status_error(self,
							status,
							"commit rejected",
							FWUPD_ERROR_SIGNATURE_INVALID,
							data,
							error);

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
fu_focal_moc_device_ensure_iap_status_layout(FuFocalMocDevice *self, GError **error)
{
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
							 &self->iap_status_layout,
							 error);
}

static gboolean
fu_focal_moc_device_ensure_version(FuFocalMocDevice *self, GError **error)
{
	gboolean is_bootloader = FALSE;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) data = NULL;

	/* never reuse a layout probed on a previous enumeration */
	self->iap_status_layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN;
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
	if (fu_focal_moc_iap_probe_required(is_bootloader,
					    fu_focal_moc_device_is_protocol_v2(self))) {
		if (!fu_focal_moc_device_ensure_iap_status_layout(self, error))
			return FALSE;
	} else if (is_bootloader) {
		/* protocol v2 bootloaders already use the unified layout */
		self->iap_status_layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_ALIGNED;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_probe(FuDevice *device, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	g_autoptr(GPtrArray) interfaces = NULL;

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->probe(device, error))
		return FALSE;

	/* the two protocols are not wire-compatible and pick mutually exclusive
	 * transport paths, so guessing one is worse than refusing a quirk entry
	 * that sets neither or both */
	if (fu_device_has_private_flag(device, FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V1) ==
	    fu_device_has_private_flag(device, FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V2)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "exactly one update-protocol flag is required");
		return FALSE;
	}
	if (fu_focal_moc_device_is_protocol_v2(self)) {
		fu_device_set_firmware_size_max(device, FU_FOCAL_MOC_V2_MAX_FIRMWARE_SIZE);
		fu_device_set_install_duration(device, FU_FOCAL_MOC_V2_INSTALL_DURATION);
	} else {
		fu_device_set_firmware_size_max(device, FU_FOCAL_MOC_V1_MAX_FIRMWARE_SIZE);
		fu_device_set_install_duration(device, FU_FOCAL_MOC_V1_INSTALL_DURATION);
	}

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
				self->ep_in = fu_usb_endpoint_get_address(endpoint);
			else
				self->ep_out = fu_usb_endpoint_get_address(endpoint);
		}
		self->iface = fu_usb_interface_get_number(iface);
		break;
	}
	if (self->ep_in == 0 || self->ep_out == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "bulk endpoint pair not found on interface 0");
		return FALSE;
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->iface);

	/* success */
	return TRUE;
}

#define FU_FOCAL_MOC_DEVICE_HOST_KEY_EVENT_ID "FuFocalMocTransportHostKey"

static gboolean
fu_focal_moc_device_transport_key_load(gpointer user_data,
				       guint8 *host_key,
				       gboolean *found,
				       GError **error)
{
	FuDevice *device = FU_DEVICE(user_data);
	FuDeviceEvent *event;
	g_autoptr(GBytes) blob = NULL;

	/* only a replay reuses a recorded key; a live device negotiates fresh */
	*found = FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	event = fu_device_load_event(device, FU_FOCAL_MOC_DEVICE_HOST_KEY_EVENT_ID, error);
	if (event == NULL)
		return FALSE;
	blob = fu_device_event_get_bytes(event, "Data", error);
	if (blob == NULL)
		return FALSE;
	if (g_bytes_get_size(blob) != FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "recorded host key size invalid: 0x%x",
			    (guint)g_bytes_get_size(blob));
		return FALSE;
	}
	if (!fu_memcpy_safe(host_key,
			    FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE,
			    0,
			    g_bytes_get_data(blob, NULL),
			    g_bytes_get_size(blob),
			    0,
			    FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE,
			    error))
		return FALSE;
	*found = TRUE;

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_transport_key_save(gpointer user_data, const guint8 *host_key, GError **error)
{
	FuDevice *device = FU_DEVICE(user_data);
	FuDeviceEvent *event;

	/* only journal the ephemeral key while recording an emulation */
	if (!fu_context_has_flag(fu_device_get_context(device), FU_CONTEXT_FLAG_SAVE_EVENTS))
		return TRUE;
	event = fu_device_save_event(device, FU_FOCAL_MOC_DEVICE_HOST_KEY_EVENT_ID);
	fu_device_event_set_data(event, "Data", host_key, FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_transport_handshake(FuFocalMocDevice *self, GError **error)
{
	if (!fu_focal_moc_transport_handshake(self->transport, error)) {
		g_prefix_error_literal(error, "transport handshake failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_device_setup(FuDevice *device, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	gboolean ret;

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->setup(device, error))
		return FALSE;
	if (fu_focal_moc_device_is_protocol_v2(self) && !fu_focal_moc_device_is_v2_iap(self)) {
		if (!fu_focal_moc_transport_is_supported()) {
			fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_set_update_error(device,
						   "TransportSec requires GnuTLS 3.8.2 or later");
			return TRUE;
		}
		if (!fu_focal_moc_device_transport_handshake(self, error))
			return FALSE;
		ret = fu_focal_moc_device_ensure_version(self, error);
		fu_focal_moc_transport_teardown(self->transport);
		return ret;
	}
	return fu_focal_moc_device_ensure_version(self, error);
}

static gboolean
fu_focal_moc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	guint8 mode = 1;
	g_autoptr(GByteArray) data = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	if (fu_focal_moc_device_is_protocol_v2(self) &&
	    !fu_focal_moc_device_transport_handshake(self, error))
		return FALSE;
	data = fu_focal_moc_device_command_ok(self,
					      FU_FOCAL_MOC_CMD_SET_BOOT_MODE,
					      &mode,
					      sizeof(mode),
					      FU_FOCAL_MOC_USB_TIMEOUT_MS,
					      "enter IAP rejected",
					      error);
	if (fu_focal_moc_device_is_protocol_v2(self))
		fu_focal_moc_transport_teardown(self->transport);
	if (data == NULL)
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_focal_moc_device_prepare_firmware(FuDevice *device,
				     FuInputStream *stream,
				     FuProgress *progress,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	g_autoptr(FuFirmware) firmware = NULL;

	/* the core size check reads fu_firmware_get_size(), which is zero for a
	 * generic firmware parsed without a cached stream; an oversized image
	 * would otherwise only fail once 255 blocks had already been flashed */
	if (!fu_focal_moc_device_is_protocol_v2(self)) {
		gsize streamsz = 0;

		if (!fu_input_stream_size(stream, &streamsz, error))
			return NULL;
		if (streamsz > FU_FOCAL_MOC_V1_MAX_FIRMWARE_SIZE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "protocol v1 firmware too large: got=0x%x maximum=0x%x",
				    (guint)streamsz,
				    (guint)FU_FOCAL_MOC_V1_MAX_FIRMWARE_SIZE);
			return NULL;
		}
	}
	if (fu_focal_moc_device_is_protocol_v2(self))
		firmware = fu_focal_moc_firmware_new();
	else
		firmware = fu_firmware_new();
	if (!fu_firmware_parse_stream(firmware, stream, 0, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_focal_moc_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	g_autoptr(FuInputStream) stream = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "firmware write requires IAP mode");
		return FALSE;
	}
	stream = fu_firmware_get_stream(firmware, error);
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
	self->transport = fu_focal_moc_transport_new(fu_focal_moc_device_send_raw,
						     fu_focal_moc_device_receive_raw,
						     self);
	fu_focal_moc_transport_set_key_journal(self->transport,
					       fu_focal_moc_device_transport_key_load,
					       fu_focal_moc_device_transport_key_save);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
	fu_device_add_protocol(FU_DEVICE(self), "com.focal.moc");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-on-chip fingerprint sensor");
}

static void
fu_focal_moc_device_finalize(GObject *object)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(object);

	fu_focal_moc_transport_free(self->transport);
	G_OBJECT_CLASS(fu_focal_moc_device_parent_class)->finalize(object);
}

static void
fu_focal_moc_device_class_init(FuFocalMocDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_focal_moc_device_finalize;
	device_class->to_string = fu_focal_moc_device_to_string;
	device_class->probe = fu_focal_moc_device_probe;
	device_class->setup = fu_focal_moc_device_setup;
	device_class->detach = fu_focal_moc_device_detach;
	device_class->prepare_firmware = fu_focal_moc_device_prepare_firmware;
	device_class->write_firmware = fu_focal_moc_device_write_firmware;
	device_class->set_progress = fu_focal_moc_device_set_progress;
	fu_device_register_private_flag(device_class, FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V1);
	fu_device_register_private_flag(device_class, FU_FOCAL_MOC_DEVICE_FLAG_UPDATE_PROTOCOL_V2);
}
