/*
 * Copyright 1999-2021 Logitech, Inc.
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <json-glib/json-glib.h>
#include <string.h>

#include "fu-logitech-bulkcontroller-child.h"
#include "fu-logitech-bulkcontroller-common.h"
#include "fu-logitech-bulkcontroller-device.h"
#include "fu-logitech-bulkcontroller-struct.h"

#define HASH_TIMEOUT		      30000
#define UPD_INTERFACE_SUBPROTOCOL_ID  117
#define SYNC_INTERFACE_SUBPROTOCOL_ID 118
#define BULK_TRANSFER_TIMEOUT	      2500
#define HASH_VALUE_SIZE		      16
#define MAX_RETRIES		      5
#define MAX_WAIT_COUNT		      150
#define POST_INSTALL_SLEEP_DURATION   80 * 1000 /* ms */

enum { EP_OUT, EP_IN, EP_LAST };

typedef enum { BULK_INTERFACE_UPD, BULK_INTERFACE_SYNC } FuLogitechBulkcontrollerBulkInterface;

struct _FuLogitechBulkcontrollerDevice {
	FuUsbDevice parent_instance;
	guint sync_ep[EP_LAST];
	guint update_ep[EP_LAST];
	guint sync_iface;
	guint update_iface;
	FuLogitechBulkcontrollerDeviceState status;
	FuLogitechBulkcontrollerUpdateState update_status;
	guint update_progress; /* percentage value */
	gboolean is_sync_transfer_in_progress;
	GString *device_info_response_json;
	gsize transfer_bufsz;
	guint32 sequence_id;
};

G_DEFINE_TYPE(FuLogitechBulkcontrollerDevice, fu_logitech_bulkcontroller_device, FU_TYPE_USB_DEVICE)

static void
fu_logitech_bulkcontroller_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "BufferSize", self->transfer_bufsz);
	fwupd_codec_string_append_hex(str, idt, "SyncIface", self->sync_iface);
	fwupd_codec_string_append_hex(str, idt, "UpdateIface", self->update_iface);
	fwupd_codec_string_append(str,
				  idt,
				  "State",
				  fu_logitech_bulkcontroller_device_state_to_string(self->status));
	fwupd_codec_string_append(
	    str,
	    idt,
	    "UpdateState",
	    fu_logitech_bulkcontroller_update_state_to_string(self->update_status));
	if (self->device_info_response_json->len > 0) {
		fwupd_codec_string_append(str,
					  idt,
					  "DeviceInfoResponse",
					  self->device_info_response_json->str);
	}
	fwupd_codec_string_append_hex(str, idt, "SequenceId", self->sequence_id);
}

static gboolean
fu_logitech_bulkcontroller_device_probe(FuDevice *device, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_VENDOR_SPECIFIC &&
		    fu_usb_interface_get_protocol(intf) == 0x1) {
			if (fu_usb_interface_get_subclass(intf) == SYNC_INTERFACE_SUBPROTOCOL_ID) {
				g_autoptr(GPtrArray) endpoints =
				    fu_usb_interface_get_endpoints(intf);
				self->sync_iface = fu_usb_interface_get_number(intf);
				if (endpoints == NULL)
					continue;
				for (guint j = 0; j < endpoints->len; j++) {
					FuUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
					if (j == EP_OUT)
						self->sync_ep[EP_OUT] =
						    fu_usb_endpoint_get_address(ep);
					else
						self->sync_ep[EP_IN] =
						    fu_usb_endpoint_get_address(ep);
				}
			} else if (fu_usb_interface_get_subclass(intf) ==
				   UPD_INTERFACE_SUBPROTOCOL_ID) {
				g_autoptr(GPtrArray) endpoints =
				    fu_usb_interface_get_endpoints(intf);
				self->update_iface = fu_usb_interface_get_number(intf);
				if (endpoints == NULL)
					continue;
				for (guint j = 0; j < endpoints->len; j++) {
					FuUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
					if (j == EP_OUT)
						self->update_ep[EP_OUT] =
						    fu_usb_endpoint_get_address(ep);
					else
						self->update_ep[EP_IN] =
						    fu_usb_endpoint_get_address(ep);
				}
			}
		}
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->update_iface);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->sync_iface);
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_send(FuLogitechBulkcontrollerDevice *self,
				       guint8 *buf,
				       gsize bufsz,
				       FuLogitechBulkcontrollerBulkInterface interface_id,
				       GError **error)
{
	gint ep;

	g_return_val_if_fail(buf != NULL, FALSE);

	if (interface_id == BULK_INTERFACE_SYNC) {
		ep = self->sync_ep[EP_OUT];
	} else if (interface_id == BULK_INTERFACE_UPD) {
		ep = self->update_ep[EP_OUT];
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "interface is invalid");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "request", buf, MIN(bufsz, 12));
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 ep,
					 buf,
					 bufsz,
					 NULL, /* transferred */
					 BULK_TRANSFER_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to send using bulk transfer: ");
		fu_error_convert(error);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_recv(FuLogitechBulkcontrollerDevice *self,
				       guint8 *buf,
				       gsize bufsz,
				       FuLogitechBulkcontrollerBulkInterface interface_id,
				       guint timeout,
				       GError **error)
{
	gint ep;
	gsize actual_length = 0;

	g_return_val_if_fail(buf != NULL, FALSE);

	if (interface_id == BULK_INTERFACE_SYNC) {
		ep = self->sync_ep[EP_IN];
	} else if (interface_id == BULK_INTERFACE_UPD) {
		ep = self->update_ep[EP_IN];
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "interface is invalid");
		return FALSE;
	}
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 ep,
					 buf,
					 bufsz,
					 &actual_length,
					 timeout,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to receive: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "response", buf, MIN(actual_length, 12));
	return TRUE;
}

typedef struct {
	FuLogitechBulkcontrollerCmd cmd;
	guint32 sequence_id;
	GByteArray *data;
} FuLogitechBulkcontrollerResponse;

static FuLogitechBulkcontrollerResponse *
fu_logitech_bulkcontroller_device_response_new(void)
{
	FuLogitechBulkcontrollerResponse *response = g_new0(FuLogitechBulkcontrollerResponse, 1);
	response->data = g_byte_array_new();
	return response;
}

static void
fu_logitech_bulkcontroller_device_response_free(FuLogitechBulkcontrollerResponse *response)
{
	if (response->data != NULL)
		g_byte_array_unref(response->data);
	g_free(response);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuLogitechBulkcontrollerResponse,
			      fu_logitech_bulkcontroller_device_response_free)

static gboolean
fu_logitech_bulkcontroller_device_sync_send_cmd(FuLogitechBulkcontrollerDevice *self,
						FuLogitechBulkcontrollerCmd cmd,
						GByteArray *buf,
						GError **error)
{
	g_autoptr(FuStructLogitechBulkcontrollerSendSyncReq) st_req =
	    fu_struct_logitech_bulkcontroller_send_sync_req_new();
	g_autofree gchar *str = NULL;

	/* increment */
	self->sequence_id++;

	/* send */
	fu_struct_logitech_bulkcontroller_send_sync_req_set_cmd(st_req, cmd);
	fu_struct_logitech_bulkcontroller_send_sync_req_set_sequence_id(st_req, self->sequence_id);
	if (buf != NULL) {
		fu_struct_logitech_bulkcontroller_send_sync_req_set_payload_length(st_req,
										   buf->len);
		g_byte_array_append(st_req, buf->data, buf->len);
	}
	str = fu_struct_logitech_bulkcontroller_send_sync_req_to_string(st_req);
	g_debug("sending: %s", str);
	if (!fu_logitech_bulkcontroller_device_send(self,
						    st_req->data,
						    st_req->len,
						    BULK_INTERFACE_SYNC,
						    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_sync_send_ack(FuLogitechBulkcontrollerDevice *self,
						FuLogitechBulkcontrollerCmd cmd,
						GError **error)
{
	g_autoptr(GByteArray) buf_ack = g_byte_array_new();
	fu_byte_array_append_uint32(buf_ack, cmd, G_LITTLE_ENDIAN);
	if (!fu_logitech_bulkcontroller_device_sync_send_cmd(self,
							     FU_LOGITECH_BULKCONTROLLER_CMD_ACK,
							     buf_ack,
							     error)) {
		g_prefix_error(error,
			       "failed to send ack for %s: ",
			       fu_logitech_bulkcontroller_cmd_to_string(cmd));
		return FALSE;
	}
	return TRUE;
}

static FuLogitechBulkcontrollerResponse *
fu_logitech_bulkcontroller_device_sync_wait_any(FuLogitechBulkcontrollerDevice *self,
						GError **error)
{
	g_autofree guint8 *buf = g_malloc0(self->transfer_bufsz);
	g_autoptr(FuStructLogitechBulkcontrollerSendSyncRes) st = NULL;
	g_autoptr(FuLogitechBulkcontrollerResponse) response =
	    fu_logitech_bulkcontroller_device_response_new();

	if (!fu_logitech_bulkcontroller_device_recv(self,
						    buf,
						    self->transfer_bufsz,
						    BULK_INTERFACE_SYNC,
						    BULK_TRANSFER_TIMEOUT,
						    error))
		return NULL;
	st = fu_struct_logitech_bulkcontroller_send_sync_res_parse(buf,
								   self->transfer_bufsz,
								   0x0,
								   error);
	if (st == NULL)
		return NULL;
	response->cmd = fu_struct_logitech_bulkcontroller_send_sync_res_get_cmd(st);
	response->sequence_id = fu_struct_logitech_bulkcontroller_send_sync_res_get_sequence_id(st);
	g_byte_array_append(response->data,
			    buf + st->len,
			    fu_struct_logitech_bulkcontroller_send_sync_res_get_payload_length(st));
	/* no payload for UninitBuffer, skip check */
	if ((response->cmd != FU_LOGITECH_BULKCONTROLLER_CMD_UNINIT_BUFFER) &&
	    (response->data->len == 0)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to receive packet");
		return NULL;
	}
	return g_steal_pointer(&response);
}

static GByteArray *
fu_logitech_bulkcontroller_device_sync_wait_cmd(FuLogitechBulkcontrollerDevice *self,
						FuLogitechBulkcontrollerCmd cmd,
						guint32 sequence_id,
						GError **error)
{
	g_autoptr(FuLogitechBulkcontrollerResponse) response = NULL;

	response = fu_logitech_bulkcontroller_device_sync_wait_any(self, error);
	if (response == NULL)
		return NULL;
	if (response->cmd != cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "command invalid, expected %s and got %s",
			    fu_logitech_bulkcontroller_cmd_to_string(cmd),
			    fu_logitech_bulkcontroller_cmd_to_string(response->cmd));
		return NULL;
	}

	/* verify the sequence ID */
	if (response->sequence_id != sequence_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "sequence ID invalid, expected 0x%04x and got 0x%04x",
			    sequence_id,
			    response->sequence_id);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&response->data);
}

static gboolean
fu_logitech_bulkcontroller_device_sync_wait_cmd_retry_cb(FuDevice *device,
							 gpointer user_data,
							 GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	FuLogitechBulkcontrollerResponse *helper = (FuLogitechBulkcontrollerResponse *)user_data;

	helper->data = fu_logitech_bulkcontroller_device_sync_wait_cmd(self,
								       helper->cmd,
								       helper->sequence_id,
								       error);
	if (helper->data == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_logitech_bulkcontroller_device_sync_wait_cmd_retry(FuLogitechBulkcontrollerDevice *self,
						      FuLogitechBulkcontrollerCmd cmd,
						      guint32 sequence_id,
						      GError **error)
{
	FuLogitechBulkcontrollerResponse helper = {.cmd = cmd, .sequence_id = sequence_id};
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_logitech_bulkcontroller_device_sync_wait_cmd_retry_cb,
			     MAX_RETRIES,
			     &helper,
			     error))
		return NULL;
	return helper.data;
}

static gboolean
fu_logitech_bulkcontroller_device_sync_check_ack_cmd(GByteArray *buf,
						     FuLogitechBulkcontrollerCmd cmd,
						     GError **error)
{
	gchar ack_payload[6] = {0x0};
	guint64 ack_cmd = 0;

	/* this is weird; base 10 number as ASCII as the ack payload... */
	if (!fu_memcpy_safe((guint8 *)ack_payload,
			    sizeof(ack_payload),
			    0x0,
			    buf->data,
			    buf->len,
			    0x0,
			    sizeof(ack_payload) - 1,
			    error)) {
		g_prefix_error(error, "failed to copy ack payload: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "ack_payload", (guint8 *)ack_payload, sizeof(ack_payload));
	if (!fu_strtoull((const gchar *)ack_payload,
			 &ack_cmd,
			 0,
			 G_MAXUINT32,
			 FU_INTEGER_BASE_AUTO,
			 error)) {
		g_prefix_error(error, "failed to parse ack payload cmd: ");
		return FALSE;
	}
	g_debug("ack_cmd: %s [0x%x]",
		fu_logitech_bulkcontroller_cmd_to_string(ack_cmd),
		(guint)ack_cmd);
	if (ack_cmd != cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "command invalid, expected %s and got %s",
			    fu_logitech_bulkcontroller_cmd_to_string(cmd),
			    fu_logitech_bulkcontroller_cmd_to_string(ack_cmd));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_sync_wait_ack_cb(FuDevice *device,
						   gpointer user_data,
						   GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	FuLogitechBulkcontrollerResponse *helper = (FuLogitechBulkcontrollerResponse *)user_data;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_logitech_bulkcontroller_device_sync_wait_cmd(self,
							      FU_LOGITECH_BULKCONTROLLER_CMD_ACK,
							      self->sequence_id,
							      error);
	if (buf == NULL)
		return FALSE;
	if (!fu_logitech_bulkcontroller_device_sync_check_ack_cmd(buf, helper->cmd, error))
		return FALSE;

	/* success */
	return TRUE;
}

/* send command and wait for ACK */
static gboolean
fu_logitech_bulkcontroller_device_sync_wait_ack(FuLogitechBulkcontrollerDevice *self,
						FuLogitechBulkcontrollerCmd cmd,
						GError **error)
{
	FuLogitechBulkcontrollerResponse helper = {.cmd = cmd};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_logitech_bulkcontroller_device_sync_wait_ack_cb,
				    10,
				    200,
				    &helper,
				    error);
}

static gboolean
fu_logitech_bulkcontroller_device_sync_check_ack(FuLogitechBulkcontrollerDevice *self,
						 FuLogitechBulkcontrollerResponse *response,
						 FuLogitechBulkcontrollerCmd cmd,
						 GError **error)
{
	/* verify the sequence ID */
	if (response->sequence_id != self->sequence_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "sequence ID invalid, expected 0x%04x and got 0x%04x",
			    self->sequence_id,
			    response->sequence_id);
		return FALSE;
	}
	return fu_logitech_bulkcontroller_device_sync_check_ack_cmd(response->data, cmd, error);
}

static GByteArray *
fu_logitech_bulkcontroller_device_sync_write(FuLogitechBulkcontrollerDevice *self,
					     GByteArray *req,
					     GError **error)
{
	g_autoptr(GByteArray) res_ack = NULL;
	g_autoptr(GByteArray) res_read = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/* send host->device buffer-write */
	if (!fu_logitech_bulkcontroller_device_sync_send_cmd(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_BUFFER_WRITE,
		req,
		error)) {
		g_prefix_error(error, "failed to send request: ");
		return NULL;
	}

	/* wait device->host ack */
	if (!fu_logitech_bulkcontroller_device_sync_wait_ack(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_BUFFER_WRITE,
		error)) {
		g_prefix_error(error, "failed to wait for ack: ");
		return NULL;
	}

	/* send host->device buffer-uninit */
	if (!fu_logitech_bulkcontroller_device_sync_send_cmd(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_UNINIT_BUFFER,
		NULL,
		error)) {
		g_prefix_error(error, "failed to uninit buffer: ");
		return NULL;
	}

	/* wait device->host buffer-read|ack */
	do {
		g_autoptr(FuLogitechBulkcontrollerResponse) response_tmp = NULL;
		response_tmp = fu_logitech_bulkcontroller_device_sync_wait_any(self, error);
		if (response_tmp == NULL) {
			g_prefix_error(error, "failed to wait for any: ");
			return NULL;
		}
		if (response_tmp->cmd == FU_LOGITECH_BULKCONTROLLER_CMD_ACK) {
			if (res_ack != NULL) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "already received ack");
				return NULL;
			}
			if (!fu_logitech_bulkcontroller_device_sync_check_ack(
				self,
				response_tmp,
				FU_LOGITECH_BULKCONTROLLER_CMD_UNINIT_BUFFER,
				error)) {
				g_prefix_error(error, "failed to check uninit buffer: ");
				return NULL;
			}
			res_ack = g_steal_pointer(&response_tmp->data);
		} else if (response_tmp->cmd == FU_LOGITECH_BULKCONTROLLER_CMD_BUFFER_READ) {
			if (res_read != NULL) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "already received read-buffer");
				return NULL;
			}
			res_read = g_steal_pointer(&response_tmp->data);
		}
	} while (res_ack == NULL || res_read == NULL);

	/* send host->device ack */
	if (!fu_logitech_bulkcontroller_device_sync_send_ack(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_BUFFER_READ,
		error)) {
		g_prefix_error(error, "failed to ack read buffer: ");
		return NULL;
	}

	/* wait device->host uninit */
	buf = fu_logitech_bulkcontroller_device_sync_wait_cmd_retry(
	    self,
	    FU_LOGITECH_BULKCONTROLLER_CMD_UNINIT_BUFFER,
	    0x0, /* why? */
	    error);
	if (buf == NULL) {
		g_prefix_error(error, "failed to wait for uninit buffer: ");
		return NULL;
	}

	/* send host->device ack */
	if (!fu_logitech_bulkcontroller_device_sync_send_ack(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_UNINIT_BUFFER,
		error)) {
		g_prefix_error(error, "failed to ack uninit buffer: ");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&res_read);
}

static gboolean
fu_logitech_bulkcontroller_device_upd_send_cmd(FuLogitechBulkcontrollerDevice *self,
					       guint32 cmd,
					       GBytes *buf,
					       guint timeout,
					       GError **error)
{
	g_autofree guint8 *buf_tmp = g_malloc0(self->transfer_bufsz);
	GByteArray buf_ack = {.data = buf_tmp, .len = self->transfer_bufsz};
	g_autoptr(FuStructLogitechBulkcontrollerUpdateReq) buf_pkt =
	    fu_struct_logitech_bulkcontroller_update_req_new();

	fu_struct_logitech_bulkcontroller_update_req_set_cmd(buf_pkt, cmd);
	if (buf != NULL) {
		fu_struct_logitech_bulkcontroller_update_req_set_payload_length(
		    buf_pkt,
		    g_bytes_get_size(buf));
		fu_byte_array_append_bytes(buf_pkt, buf);
	}
	if (!fu_logitech_bulkcontroller_device_send(self,
						    buf_pkt->data,
						    buf_pkt->len,
						    BULK_INTERFACE_UPD,
						    error))
		return FALSE;

	/* receiving INIT ACK */
	if (!fu_logitech_bulkcontroller_device_recv(self,
						    buf_tmp,
						    self->transfer_bufsz,
						    BULK_INTERFACE_UPD,
						    timeout,
						    error))
		return FALSE;
	if (fu_struct_logitech_bulkcontroller_update_res_get_cmd(&buf_ack) !=
	    FU_LOGITECH_BULKCONTROLLER_CMD_ACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "not CMD_ACK, got %s",
			    fu_logitech_bulkcontroller_cmd_to_string(
				fu_struct_logitech_bulkcontroller_update_res_get_cmd(&buf_ack)));
		return FALSE;
	}
	if (fu_struct_logitech_bulkcontroller_update_res_get_cmd_req(&buf_ack) != cmd) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "invalid upd message received, expected %s, got %s",
		    fu_logitech_bulkcontroller_cmd_to_string(cmd),
		    fu_logitech_bulkcontroller_cmd_to_string(
			fu_struct_logitech_bulkcontroller_update_res_get_cmd_req(&buf_ack)));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_compute_hash_cb(const guint8 *buf,
						  gsize bufsz,
						  gpointer user_data,
						  GError **error)
{
	GChecksum *checksum = (GChecksum *)user_data;
	g_checksum_update(checksum, buf, bufsz);
	return TRUE;
}

static gchar *
fu_logitech_bulkcontroller_device_compute_hash(GInputStream *stream, GError **error)
{
	guint8 md5buf[HASH_VALUE_SIZE] = {0};
	gsize data_len = sizeof(md5buf);
	g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_MD5);
	if (!fu_input_stream_chunkify(stream,
				      fu_logitech_bulkcontroller_device_compute_hash_cb,
				      checksum,
				      error))
		return NULL;
	g_checksum_get_digest(checksum, (guint8 *)&md5buf, &data_len);
	return g_base64_encode(md5buf, sizeof(md5buf));
}

static FwupdStatus
fu_logitech_bulkcontroller_device_update_state_to_status(
    FuLogitechBulkcontrollerUpdateState update_state)
{
	if (update_state == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_DOWNLOADING)
		return FWUPD_STATUS_DEVICE_WRITE;
	if (update_state == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_STARTING)
		return FWUPD_STATUS_DEVICE_VERIFY;
	if (update_state == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_UPDATING)
		return FWUPD_STATUS_DEVICE_WRITE;
	if (update_state == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_CURRENT)
		return FWUPD_STATUS_IDLE;
	return FWUPD_STATUS_UNKNOWN;
}

static gboolean
fu_logitech_bulkcontroller_device_ensure_child(FuLogitechBulkcontrollerDevice *self,
					       JsonObject *json_device,
					       GError **error)
{
	FuLogitechBulkcontrollerDeviceState status;
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));
	g_autoptr(FuDevice) child = NULL;
	const gchar *name;
	const gchar *required_members[] = {
	    "make",
	    "model",
	    "name",
	    "status",
	    "sw",
	    "type",
	    "vid",
	};

	/* sanity check */
	for (guint i = 0; i < G_N_ELEMENTS(required_members); i++) {
		if (!json_object_has_member(json_device, required_members[i])) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no %s",
				    required_members[i]);
			return FALSE;
		}
	}
	if (g_strcmp0(json_object_get_string_member(json_device, "type"), "Sentinel") != 0)
		return TRUE;

	/* check status */
	status = json_object_get_int_member(json_device, "status");
	if (status != FU_LOGITECH_BULKCONTROLLER_DEVICE_STATE_ONLINE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "status is %s",
			    fu_logitech_bulkcontroller_device_state_to_string(status));
		return FALSE;
	}

	/* child already exists */
	name = json_object_get_string_member(json_device, "name");
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child_tmp = g_ptr_array_index(children, i);
		if (g_strcmp0(fu_device_get_logical_id(child_tmp), name) == 0) {
			g_debug("found existing %s device, just updating version", name);
			fu_device_set_version(child_tmp,
					      json_object_get_string_member(json_device, "sw"));
			return TRUE;
		}
	}

	/* create new child */
	child = g_object_new(FU_TYPE_LOGITECH_BULKCONTROLLER_CHILD, "proxy", self, NULL);
	fu_device_add_private_flag(child, FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_set_name(child, name);
	fu_device_set_vendor(child, json_object_get_string_member(json_device, "make"));
	fu_device_set_logical_id(child, name);
	fu_device_set_version(child, json_object_get_string_member(json_device, "sw"));
	if (json_object_has_member(json_device, "serial"))
		fu_device_set_serial(child, json_object_get_string_member(json_device, "serial"));
	fu_device_add_instance_strup(child,
				     "MODEL",
				     json_object_get_string_member(json_device, "model"));
	if (!fu_device_build_instance_id(child, error, "LOGI", "MODEL", NULL))
		return FALSE;
	fu_device_add_child(FU_DEVICE(self), child);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_json_parser(FuLogitechBulkcontrollerDevice *self,
					      GByteArray *decoded_pkt,
					      GError **error)
{
	JsonArray *json_devices;
	JsonNode *json_root;
	JsonObject *json_device;
	JsonObject *json_object;
	JsonObject *json_payload;
	g_autoptr(JsonParser) json_parser = json_parser_new();

	/* parse JSON reply */
	if (!json_parser_load_from_data(json_parser,
					(const gchar *)decoded_pkt->data,
					decoded_pkt->len,
					error)) {
		g_prefix_error(error, "failed to parse json data: ");
		return FALSE;
	}
	json_root = json_parser_get_root(json_parser);
	if (json_root == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "did not get JSON root");
		return FALSE;
	}
	json_object = json_node_get_object(json_root);
	json_payload = json_object_get_object_member(json_object, "payload");
	if (json_payload == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "did not get JSON payload");
		return FALSE;
	}
	json_devices = json_object_get_array_member(json_payload, "devices");
	if (json_devices == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "did not get JSON devices");
		return FALSE;
	}
	json_device = json_array_get_object_element(json_devices, 0);
	if (json_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "did not get JSON device");
		return FALSE;
	}
	if (json_object_has_member(json_device, "name"))
		fu_device_set_name(FU_DEVICE(self),
				   json_object_get_string_member(json_device, "name"));
	if (json_object_has_member(json_device, "sw"))
		fu_device_set_version(FU_DEVICE(self),
				      json_object_get_string_member(json_device, "sw"));
	if (json_object_has_member(json_device, "type")) {
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       json_object_get_string_member(json_device, "type"),
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS);
	}
	if (json_object_has_member(json_device, "status"))
		self->status = json_object_get_int_member(json_device, "status");
	if (json_object_has_member(json_device, "updateStatus"))
		self->update_status = json_object_get_int_member(json_device, "updateStatus");
	/* updateProgress only available while firmware upgrade is going on */
	if (json_object_has_member(json_device, "updateProgress"))
		self->update_progress = json_object_get_int_member(json_device, "updateProgress");

	/* ensure child pheripheral devices exist */
	for (guint i = 1; i < json_array_get_length(json_devices); i++) {
		JsonObject *json_child = json_array_get_object_element(json_devices, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_logitech_bulkcontroller_device_ensure_child(self, json_child, &error_local))
			g_warning("failed to add child: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_parse_info(FuLogitechBulkcontrollerDevice *self,
					     GByteArray *buf,
					     GError **error)
{
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;
	g_autofree gchar *bufstr = NULL;
	g_autoptr(GByteArray) decoded_pkt = NULL;

	decoded_pkt = fu_logitech_bulkcontroller_proto_manager_decode_message(buf->data,
									      buf->len,
									      &proto_id,
									      error);
	if (decoded_pkt == NULL) {
		g_prefix_error(error, "failed to unpack packet for device info request: ");
		return FALSE;
	}
	bufstr = fu_strsafe((const gchar *)decoded_pkt->data, decoded_pkt->len);
	g_debug("received device response: id: %u, length %u, data: %s",
		proto_id,
		buf->len,
		bufstr);
	if (proto_id != kProtoId_GetDeviceInfoResponse && proto_id != kProtoId_KongEvent) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "incorrect response for device info request");
		return FALSE;
	}
	if (!fu_logitech_bulkcontroller_device_json_parser(self, decoded_pkt, error))
		return FALSE;

	/* success */
	g_string_assign(self->device_info_response_json, bufstr);
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_ensure_info_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autoptr(GByteArray) buf = NULL;
	gboolean send_req = *(gboolean *)user_data;

	/* sending GetDeviceInfoRequest. Device reports quite a few matrix, including status,
	 * progress etc
	 * Two ways to get data from device:
	 * 1. Listen for the data broadcasted by device, while firmware upgrade is going on
	 * 2. Make explicit request to the device. Used when data is needed before/after firmware
	 * upgrade
	 */
	if (send_req) {
		g_autoptr(GByteArray) device_request =
		    fu_logitech_bulkcontroller_proto_manager_generate_get_device_info_request(
			device);
		buf = fu_logitech_bulkcontroller_device_sync_write(self, device_request, error);
		if (buf == NULL)
			return FALSE;
	} else {
		/* poll the out interface */
		buf = fu_logitech_bulkcontroller_device_sync_wait_cmd(
		    self,
		    FU_LOGITECH_BULKCONTROLLER_CMD_BUFFER_READ,
		    0x0, /* sequence_id */
		    error);
		if (buf == NULL)
			return FALSE;
	}
	return fu_logitech_bulkcontroller_device_parse_info(self, buf, error);
}

static gboolean
fu_logitech_bulkcontroller_device_ensure_info(FuLogitechBulkcontrollerDevice *self,
					      gboolean send_req,
					      GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_logitech_bulkcontroller_device_ensure_info_cb,
			       MAX_RETRIES,
			       &send_req,
			       error);
}

static gboolean
fu_logitech_bulkcontroller_device_upd_send_init_cmd_cb(FuDevice *device,
						       gpointer user_data,
						       GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	return fu_logitech_bulkcontroller_device_upd_send_cmd(self,
							      FU_LOGITECH_BULKCONTROLLER_CMD_INIT,
							      NULL,
							      BULK_TRANSFER_TIMEOUT,
							      error);
}

static gboolean
fu_logitech_bulkcontroller_device_write_fw(FuLogitechBulkcontrollerDevice *self,
					   GInputStream *stream,
					   FuProgress *progress,
					   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(
	    stream,
	    FU_CHUNK_ADDR_OFFSET_NONE,
	    FU_CHUNK_PAGESZ_NONE,
	    self->transfer_bufsz - FU_STRUCT_LOGITECH_BULKCONTROLLER_UPDATE_REQ_SIZE,
	    error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) chk_blob = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		chk_blob = fu_chunk_get_bytes(chk);
		if (!fu_logitech_bulkcontroller_device_upd_send_cmd(
			self,
			FU_LOGITECH_BULKCONTROLLER_CMD_DATA_TRANSFER,
			chk_blob,
			BULK_TRANSFER_TIMEOUT,
			error)) {
			g_prefix_error(error, "failed to send data packet 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_verify_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	FuProgress *progress = FU_PROGRESS(user_data);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/* poll the out interface */
	buf = fu_logitech_bulkcontroller_device_sync_wait_cmd(
	    self,
	    FU_LOGITECH_BULKCONTROLLER_CMD_BUFFER_READ,
	    0x0, /* sequence_id */
	    &error_local);
	if (buf == NULL) {
		g_autoptr(GByteArray) device_request = NULL;
		g_debug("manually requesting as no pending request: %s", error_local->message);
		device_request =
		    fu_logitech_bulkcontroller_proto_manager_generate_get_device_info_request(
			device);
		buf = fu_logitech_bulkcontroller_device_sync_write(self, device_request, error);
		if (buf == NULL)
			return FALSE;
	}
	if (!fu_logitech_bulkcontroller_device_parse_info(self, buf, error))
		return FALSE;

	g_debug("firmware update status: %s, progress: %u",
		fu_logitech_bulkcontroller_update_state_to_string(self->update_status),
		self->update_progress);
	fu_progress_set_status(
	    progress,
	    fu_logitech_bulkcontroller_device_update_state_to_status(self->update_status));

	/* existing device image version is same as newly pushed image? */
	if (self->update_status == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_ERROR ||
	    self->update_status == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_CURRENT)
		return TRUE;

	/* only update the child if the percentage is bigger -- which means the progressbar
	 * may stall, but will never go backwards */
	if (self->update_progress > fu_progress_get_percentage(progress))
		fu_progress_set_percentage(progress, self->update_progress);

	/* keep waiting */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "waiting for verify to finish");
	return FALSE;
}

static gboolean
fu_logitech_bulkcontroller_device_write_firmware(FuDevice *device,
						 FuFirmware *firmware,
						 FuProgress *progress,
						 FwupdInstallFlags flags,
						 GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	gsize streamsz = 0;
	g_autofree gchar *base64hash = NULL;
	g_autoptr(GByteArray) end_pkt = g_byte_array_new();
	g_autoptr(GByteArray) start_pkt = g_byte_array_new();
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GBytes) end_pkt_blob = NULL;
	g_autoptr(GBytes) start_pkt_blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 55, "device-write-blocks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "end-transfer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "uninit");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 40, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* sending INIT. Retry if device is not in IDLE state to receive the file */
	if (!fu_device_retry(device,
			     fu_logitech_bulkcontroller_device_upd_send_init_cmd_cb,
			     MAX_RETRIES,
			     NULL,
			     error)) {
		g_prefix_error(error,
			       "failed to write init transfer packet: please reboot the device: ");
		return FALSE;
	}

	/* transfer sent */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	fu_byte_array_append_uint64(start_pkt, streamsz, G_LITTLE_ENDIAN);
	start_pkt_blob = g_bytes_new(start_pkt->data, start_pkt->len);
	if (!fu_logitech_bulkcontroller_device_upd_send_cmd(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_START_TRANSFER,
		start_pkt_blob,
		BULK_TRANSFER_TIMEOUT,
		error)) {
		g_prefix_error(error, "failed to write start transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* push each block to device */
	if (!fu_logitech_bulkcontroller_device_write_fw(self,
							stream,
							fu_progress_get_child(progress),
							error)) {
		g_prefix_error(error, "failed to write firmware: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* sending end transfer -- extend the bulk transfer timeout value, as android device takes
	 * some time to calculate the hash and respond */
	base64hash = fu_logitech_bulkcontroller_device_compute_hash(stream, error);
	if (base64hash == NULL)
		return FALSE;
	fu_byte_array_append_uint32(end_pkt, 1, G_LITTLE_ENDIAN); /* update */
	fu_byte_array_append_uint32(end_pkt, 0, G_LITTLE_ENDIAN); /* force */
	fu_byte_array_append_uint32(end_pkt,
				    FU_LOGITECH_BULKCONTROLLER_CHECKSUM_TYPE_MD5,
				    G_LITTLE_ENDIAN);
	g_byte_array_append(end_pkt, (const guint8 *)base64hash, strlen(base64hash));
	end_pkt_blob = g_bytes_new(end_pkt->data, end_pkt->len);
	if (!fu_logitech_bulkcontroller_device_upd_send_cmd(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_END_TRANSFER,
		end_pkt_blob,
		HASH_TIMEOUT,
		error)) {
		g_prefix_error(error, "failed to write end transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send uninit */
	if (!fu_logitech_bulkcontroller_device_upd_send_cmd(self,
							    FU_LOGITECH_BULKCONTROLLER_CMD_UNINIT,
							    NULL,
							    BULK_TRANSFER_TIMEOUT,
							    error)) {
		g_prefix_error(error, "failed to write finish transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/*
	 * image file pushed. Device validates and uploads new image on inactive partition.
	 * Restart sync cb, to get the update progress
	 * Normally status changes as follows:
	 *  While image being pushed: kUpdateStateCurrent->kUpdateStateDownloading (~5minutes)
	 *  After image push is complete: kUpdateStateDownloading->kUpdateStateReady
	 *  Validating image: kUpdateStateReady->kUpdateStateStarting
	 *  Uploading image: kUpdateStateStarting->kUpdateStateUpdating
	 *  Upload finished: kUpdateStateUpdating->kUpdateStateCurrent (~5minutes)
	 *  After upload is finished, device reboots itself
	 */
	if (!fu_device_retry_full(device,
				  fu_logitech_bulkcontroller_device_verify_cb,
				  500,	/* over 10 minutes */
				  2500, /* ms */
				  fu_progress_get_child(progress),
				  error))
		return FALSE;
	if (self->update_status == FU_LOGITECH_BULKCONTROLLER_UPDATE_STATE_ERROR) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware upgrade failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_set_time_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;
	g_autofree gchar *bufstr = NULL;
	g_autoptr(GByteArray) decoded_pkt = NULL;
	g_autoptr(GByteArray) device_request = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/* send SetDeviceTimeRequest to sync device clock with host */
	device_request =
	    fu_logitech_bulkcontroller_proto_manager_generate_set_device_time_request(device,
										      error);
	if (device_request == NULL)
		return FALSE;
	buf = fu_logitech_bulkcontroller_device_sync_write(self, device_request, error);
	if (buf == NULL)
		return FALSE;
	decoded_pkt = fu_logitech_bulkcontroller_proto_manager_decode_message(buf->data,
									      buf->len,
									      &proto_id,
									      error);
	if (decoded_pkt == NULL) {
		g_prefix_error(error, "failed to unpack packet: ");
		return FALSE;
	}
	bufstr = fu_strsafe((const gchar *)decoded_pkt->data, decoded_pkt->len);
	g_debug("received device response while processing: id: %u, length %u, data: %s",
		proto_id,
		buf->len,
		bufstr);
	if (proto_id != kProtoId_Ack) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "incorrect response");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_set_time(FuLogitechBulkcontrollerDevice *self, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_logitech_bulkcontroller_device_set_time_cb,
			       MAX_RETRIES,
			       NULL,
			       error);
}

static gboolean
fu_logitech_bulkcontroller_device_transition_to_device_mode_cb(FuDevice *device,
							       gpointer user_data,
							       GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;
	g_autoptr(GByteArray) req = NULL;
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(GByteArray) decoded_pkt = NULL;

	req = fu_logitech_bulkcontroller_proto_manager_generate_transition_to_device_mode_request(
	    device);
	res = fu_logitech_bulkcontroller_device_sync_write(self, req, error);
	if (res == NULL)
		return FALSE;

	decoded_pkt = fu_logitech_bulkcontroller_proto_manager_decode_message(res->data,
									      res->len,
									      &proto_id,
									      error);
	if (decoded_pkt == NULL) {
		g_prefix_error(error, "failed to unpack packet: ");
		return FALSE;
	}
	g_debug("received transition mode response: id: %u, length %u", proto_id, res->len);
	if (proto_id != kProtoId_TransitionToDeviceModeResponse) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "incorrect response");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_transition_to_device_mode(FuLogitechBulkcontrollerDevice *self,
							    GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_logitech_bulkcontroller_device_transition_to_device_mode_cb,
			       MAX_RETRIES,
			       NULL,
			       error);
}

static gboolean
fu_logitech_bulkcontroller_device_clear_queue_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autofree guint8 *buf = g_malloc0(self->transfer_bufsz);
	g_autoptr(GError) error_local = NULL;

	if (!fu_logitech_bulkcontroller_device_recv(self,
						    buf,
						    self->transfer_bufsz,
						    BULK_INTERFACE_SYNC,
						    250, /* ms */
						    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_debug("timed out successfully");
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* failed */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "got valid data, so keep going");
	return FALSE;
}

static gboolean
fu_logitech_bulkcontroller_device_clear_queue(FuLogitechBulkcontrollerDevice *self, GError **error)
{
	g_debug("clearing any bulk data");
	return fu_device_retry(FU_DEVICE(self),
			       fu_logitech_bulkcontroller_device_clear_queue_cb,
			       3,
			       NULL,
			       error);
}

static gboolean
fu_logitech_bulkcontroller_device_check_buffer_size(FuLogitechBulkcontrollerDevice *self,
						    GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error_local = NULL;

	if (!fu_logitech_bulkcontroller_device_sync_send_cmd(
		self,
		FU_LOGITECH_BULKCONTROLLER_CMD_CHECK_BUFFERSIZE,
		NULL, /* data */
		error)) {
		g_prefix_error(error, "failed to send request: ");
		return FALSE;
	}
	buf = fu_logitech_bulkcontroller_device_sync_wait_cmd_retry(
	    self,
	    FU_LOGITECH_BULKCONTROLLER_CMD_CHECK_BUFFERSIZE,
	    0x0, /* always zero */
	    &error_local);
	if (buf != NULL) {
		self->transfer_bufsz = 16 * 1024;
	} else {
		g_debug("sticking to 8k buffersize: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_setup(FuDevice *device, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_bulkcontroller_device_parent_class)
		 ->setup(device, error)) {
		g_prefix_error(error, "failed to FuUsbDevice->setup: ");
		return FALSE;
	}

	/* empty the queue */
	if (!fu_logitech_bulkcontroller_device_clear_queue(self, error)) {
		g_prefix_error(error, "failed to clear queue: ");
		return FALSE;
	}

	/* check if the device supports a 16kb transfer buffer */
	if (fu_device_has_private_flag(device,
				       FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_CHECK_BUFFER_SIZE)) {
		if (!fu_logitech_bulkcontroller_device_check_buffer_size(self, error)) {
			g_prefix_error(error, "failed to check buffer size: ");
			return FALSE;
		}
	}

	/* device supports modes of Device (supported), Appliance and BYOD (both unsupported) */
	if (!fu_logitech_bulkcontroller_device_transition_to_device_mode(self, error)) {
		g_prefix_error(error, "failed to transition to device_mode: ");
		return FALSE;
	}

	/* the hardware is unable to handle requests -- firmware issue */
	if (fu_device_has_private_flag(device,
				       FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_POST_INSTALL)) {
		fu_device_sleep(device, POST_INSTALL_SLEEP_DURATION);
		fu_device_remove_private_flag(device,
					      FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_POST_INSTALL);
	}

	/* set device time */
	if (!fu_logitech_bulkcontroller_device_set_time(self, error)) {
		g_prefix_error(error, "failed to set time: ");
		return FALSE;
	}

	/* load current device data */
	if (!fu_logitech_bulkcontroller_device_ensure_info(self, TRUE, error)) {
		g_prefix_error(error, "failed to ensure info: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_logitech_bulkcontroller_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "reload");
}

static void
fu_logitech_bulkcontroller_device_init(FuLogitechBulkcontrollerDevice *self)
{
	self->transfer_bufsz = 8 * 1024;
	self->device_info_response_json = g_string_new(NULL);
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.proto");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_usb_device_set_claim_retry_count(FU_USB_DEVICE(self), 100);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_set_remove_delay(FU_DEVICE(self), 10 * 60 * 1000); /* >1 min to finish init */
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_CHECK_BUFFER_SIZE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_POST_INSTALL);

	/* these are unrecoverable */
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, NULL);
	fu_device_retry_add_recovery(FU_DEVICE(self),
				     FWUPD_ERROR,
				     FWUPD_ERROR_PERMISSION_DENIED,
				     NULL);
}

static void
fu_logitech_bulkcontroller_device_finalize(GObject *object)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(object);
	g_string_free(self->device_info_response_json, TRUE);
	G_OBJECT_CLASS(fu_logitech_bulkcontroller_device_parent_class)->finalize(object);
}

static void
fu_logitech_bulkcontroller_device_class_init(FuLogitechBulkcontrollerDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_logitech_bulkcontroller_device_finalize;
	device_class->to_string = fu_logitech_bulkcontroller_device_to_string;
	device_class->write_firmware = fu_logitech_bulkcontroller_device_write_firmware;
	device_class->probe = fu_logitech_bulkcontroller_device_probe;
	device_class->setup = fu_logitech_bulkcontroller_device_setup;
	device_class->set_progress = fu_logitech_bulkcontroller_device_set_progress;
}
