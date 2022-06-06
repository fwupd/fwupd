/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <json-glib/json-glib.h>
#include <string.h>

#include "fu-logitech-bulkcontroller-common.h"
#include "fu-logitech-bulkcontroller-device.h"

/* SYNC interface follows TLSV (Type, Length, SequenceID, Value) protocol */
/* UPD interface follows TLV (Type, Length, Value) protocol */
/* Payload size limited to 8k for both interfaces */
#define UPD_PACKET_HEADER_SIZE	      (2 * sizeof(guint32))
#define SYNC_PACKET_HEADER_SIZE	      (3 * sizeof(guint32))
#define HASH_TIMEOUT		      30000
#define MAX_DATA_SIZE		      8192 /* 8k */
#define PAYLOAD_SIZE		      MAX_DATA_SIZE - UPD_PACKET_HEADER_SIZE
#define UPD_INTERFACE_SUBPROTOCOL_ID  117
#define SYNC_INTERFACE_SUBPROTOCOL_ID 118
#define BULK_TRANSFER_TIMEOUT	      1000
#define HASH_VALUE_SIZE		      16
#define LENGTH_OFFSET		      0x4
#define COMMAND_OFFSET		      0x0
#define SYNC_ACK_PAYLOAD_LENGTH	      5
#define MAX_RETRIES		      5
#define MAX_HANDSHAKE_RETRIES	      3
#define MAX_WAIT_COUNT		      150

enum { SHA_256, SHA_512, MD5 };

enum { EP_OUT, EP_IN, EP_LAST };

enum { BULK_INTERFACE_UPD, BULK_INTERFACE_SYNC };

typedef enum {
	CMD_CHECK_BUFFERSIZE = 0xCC00,
	CMD_INIT = 0xCC01,
	CMD_START_TRANSFER = 0xCC02,
	CMD_DATA_TRANSFER = 0xCC03,
	CMD_END_TRANSFER = 0xCC04,
	CMD_UNINIT = 0xCC05,
	CMD_BUFFER_READ = 0xCC06,
	CMD_BUFFER_WRITE = 0xCC07,
	CMD_UNINIT_BUFFER = 0xCC08,
	CMD_ACK = 0xFF01,
	CMD_TIMEOUT = 0xFF02,
	CMD_NACK = 0xFF03
} UsbCommands;

struct _FuLogitechBulkcontrollerDevice {
	FuUsbDevice parent_instance;
	guint sync_ep[EP_LAST];
	guint update_ep[EP_LAST];
	guint sync_iface;
	guint update_iface;
	FuLogitechBulkcontrollerDeviceStatus status;
	FuLogitechBulkcontrollerDeviceUpdateState update_status;
	guint update_progress; /* percentage value */
	gboolean is_sync_transfer_in_progress;
};

typedef struct {
	FuLogitechBulkcontrollerDevice *self; /* no-ref */
	GByteArray *device_response;
	GByteArray *buf_pkt;
	GMainLoop *loop;
	GError *error;
} FuLogitechBulkcontrollerHelper;

G_DEFINE_TYPE(FuLogitechBulkcontrollerDevice, fu_logitech_bulkcontroller_device, FU_TYPE_USB_DEVICE)

static void
fu_logitech_bulkcontroller_helper_free(FuLogitechBulkcontrollerHelper *helper)
{
	if (helper->error != NULL)
		g_error_free(helper->error);
	g_byte_array_unref(helper->buf_pkt);
	g_byte_array_unref(helper->device_response);
	g_main_loop_unref(helper->loop);
	g_slice_free(FuLogitechBulkcontrollerHelper, helper);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuLogitechBulkcontrollerHelper,
			      fu_logitech_bulkcontroller_helper_free)
#pragma clang diagnostic pop

static void
fu_logitech_bulkcontroller_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	fu_string_append_kx(str, idt, "SyncIface", self->sync_iface);
	fu_string_append_kx(str, idt, "UpdateIface", self->update_iface);
	fu_string_append(str,
			 idt,
			 "Status",
			 fu_logitech_bulkcontroller_device_status_to_string(self->status));
	fu_string_append(
	    str,
	    idt,
	    "UpdateState",
	    fu_logitech_bulkcontroller_device_update_state_to_string(self->update_status));
}

static gboolean
fu_logitech_bulkcontroller_device_probe(FuDevice *device, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 3)
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = g_usb_device_get_interfaces(fu_usb_device_get_dev(FU_USB_DEVICE(self)), error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (g_usb_interface_get_class(intf) == G_USB_DEVICE_CLASS_VENDOR_SPECIFIC &&
		    g_usb_interface_get_protocol(intf) == 0x1) {
			if (g_usb_interface_get_subclass(intf) == SYNC_INTERFACE_SUBPROTOCOL_ID) {
				g_autoptr(GPtrArray) endpoints =
				    g_usb_interface_get_endpoints(intf);
				self->sync_iface = g_usb_interface_get_number(intf);
				if (endpoints == NULL)
					continue;
				for (guint j = 0; j < endpoints->len; j++) {
					GUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
					if (j == EP_OUT)
						self->sync_ep[EP_OUT] =
						    g_usb_endpoint_get_address(ep);
					else
						self->sync_ep[EP_IN] =
						    g_usb_endpoint_get_address(ep);
				}
			} else if (g_usb_interface_get_subclass(intf) ==
				   UPD_INTERFACE_SUBPROTOCOL_ID) {
				g_autoptr(GPtrArray) endpoints =
				    g_usb_interface_get_endpoints(intf);
				self->update_iface = g_usb_interface_get_number(intf);
				if (endpoints == NULL)
					continue;
				for (guint j = 0; j < endpoints->len; j++) {
					GUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
					if (j == EP_OUT)
						self->update_ep[EP_OUT] =
						    g_usb_endpoint_get_address(ep);
					else
						self->update_ep[EP_IN] =
						    g_usb_endpoint_get_address(ep);
				}
			}
		}
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->update_iface);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->sync_iface);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "this version of GUsb is not supported");
	return FALSE;
#endif
}

static gboolean
fu_logitech_bulkcontroller_device_send(FuLogitechBulkcontrollerDevice *self,
				       GByteArray *buf,
				       gint interface_id,
				       GError **error)
{
	gsize transferred = 0;
	gint ep;
	GCancellable *cancellable = NULL;
	g_return_val_if_fail(buf != NULL, FALSE);

	if (interface_id == BULK_INTERFACE_SYNC) {
		ep = self->sync_ep[EP_OUT];
	} else if (interface_id == BULK_INTERFACE_UPD) {
		ep = self->update_ep[EP_OUT];
	} else {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "interface is invalid");
		return FALSE;
	}
	if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					ep,
					(guint8 *)buf->data,
					buf->len,
					&transferred,
					BULK_TRANSFER_TIMEOUT,
					cancellable,
					error)) {
		g_prefix_error(error, "failed to send using bulk transfer: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_recv(FuLogitechBulkcontrollerDevice *self,
				       GByteArray *buf,
				       gint interface_id,
				       guint timeout,
				       GError **error)
{
	gsize received_length = 0;
	gint ep;
	g_return_val_if_fail(buf != NULL, FALSE);

	if (interface_id == BULK_INTERFACE_SYNC) {
		ep = self->sync_ep[EP_IN];
	} else if (interface_id == BULK_INTERFACE_UPD) {
		ep = self->update_ep[EP_IN];
	} else {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "interface is invalid");
		return FALSE;
	}
	if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					ep,
					buf->data,
					buf->len,
					&received_length,
					timeout,
					NULL,
					error)) {
		g_prefix_error(error, "failed to receive using bulk transfer: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_send_upd_cmd(FuLogitechBulkcontrollerDevice *self,
					       guint32 cmd,
					       GByteArray *buf,
					       GError **error)
{
	guint32 cmd_tmp = 0x0;
	guint timeout = BULK_TRANSFER_TIMEOUT;
	g_autoptr(GByteArray) buf_pkt = g_byte_array_new();
	g_autoptr(GByteArray) buf_ack = g_byte_array_new();

	fu_byte_array_append_uint32(buf_pkt, cmd, G_LITTLE_ENDIAN); /* Type(T) : Command type */
	fu_byte_array_append_uint32(buf_pkt,
				    buf != NULL ? buf->len : 0,
				    G_LITTLE_ENDIAN); /*Length(L) : Length of payload */
	if (buf != NULL) {
		g_byte_array_append(buf_pkt,
				    buf->data,
				    buf->len); /* Value(V) : Actual payload data */
	}
	if (!fu_logitech_bulkcontroller_device_send(self, buf_pkt, BULK_INTERFACE_UPD, error))
		return FALSE;

	/* receiving INIT ACK */
	fu_byte_array_set_size(buf_ack, MAX_DATA_SIZE, 0x00);

	/* extending the bulk transfer timeout value, as android device takes some time to
	   calculate Hash and respond */
	if (CMD_END_TRANSFER == cmd)
		timeout = HASH_TIMEOUT;

	if (!fu_logitech_bulkcontroller_device_recv(self,
						    buf_ack,
						    BULK_INTERFACE_UPD,
						    timeout,
						    error))
		return FALSE;

	if (!fu_memread_uint32_safe(buf_ack->data,
				    buf_ack->len,
				    COMMAND_OFFSET,
				    &cmd_tmp,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (cmd_tmp != CMD_ACK) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "not CMD_ACK, got %x", cmd);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf_ack->data,
				    buf_ack->len,
				    UPD_PACKET_HEADER_SIZE,
				    &cmd_tmp,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (cmd_tmp != cmd) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid upd message received, expected %x, got %x",
			    cmd,
			    cmd_tmp);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_send_sync_cmd(FuLogitechBulkcontrollerDevice *self,
						guint32 cmd,
						GByteArray *buf,
						GError **error)
{
	g_autoptr(GByteArray) buf_pkt = g_byte_array_new();

	fu_byte_array_append_uint32(buf_pkt, cmd, G_LITTLE_ENDIAN); /* Type(T) : Command type */
	fu_byte_array_append_uint32(buf_pkt,
				    buf != NULL ? buf->len : 0,
				    G_LITTLE_ENDIAN); /*Length(L) : Length of payload */
	fu_byte_array_append_uint32(buf_pkt,
				    g_random_int_range(0, G_MAXUINT16),
				    G_LITTLE_ENDIAN); /*Sequence(S) : Sequence ID of the data */
	if (buf != NULL) {
		g_byte_array_append(buf_pkt,
				    buf->data,
				    buf->len); /* Value(V) : Actual payload data */
	}
	if (!fu_logitech_bulkcontroller_device_send(self, buf_pkt, BULK_INTERFACE_SYNC, error))
		return FALSE;
	return TRUE;
}

static gchar *
fu_logitech_bulkcontroller_device_compute_hash(GBytes *data)
{
	guint8 md5buf[HASH_VALUE_SIZE] = {0};
	gsize data_len = sizeof(md5buf);
	GChecksum *checksum = g_checksum_new(G_CHECKSUM_MD5);
	g_checksum_update(checksum, g_bytes_get_data(data, NULL), g_bytes_get_size(data));
	g_checksum_get_digest(checksum, (guint8 *)&md5buf, &data_len);
	return g_base64_encode(md5buf, sizeof(md5buf));
}

static gboolean
fu_logitech_bulkcontroller_device_json_parser(FuDevice *device,
					      GByteArray *decoded_pkt,
					      GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
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
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "did not get JSON root");
		return FALSE;
	}
	json_object = json_node_get_object(json_root);
	json_payload = json_object_get_object_member(json_object, "payload");
	if (json_payload == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "did not get JSON payload");
		return FALSE;
	}
	json_devices = json_object_get_array_member(json_payload, "devices");
	if (json_devices == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "did not get JSON devices");
		return FALSE;
	}
	json_device = json_array_get_object_element(json_devices, 0);
	if (json_device == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "did not get JSON device");
		return FALSE;
	}
	if (json_object_has_member(json_device, "name"))
		fu_device_set_name(device, json_object_get_string_member(json_device, "name"));
	if (json_object_has_member(json_device, "sw"))
		fu_device_set_version(device, json_object_get_string_member(json_device, "sw"));
	if (json_object_has_member(json_device, "type"))
		fu_device_add_instance_id(device,
					  json_object_get_string_member(json_device, "type"));
	if (json_object_has_member(json_device, "status"))
		self->status = json_object_get_int_member(json_device, "status");
	if (json_object_has_member(json_device, "updateStatus"))
		self->update_status = json_object_get_int_member(json_device, "updateStatus");
	/* updateProgress only available while firmware upgrade is going on */
	if (json_object_has_member(json_device, "updateProgress"))
		self->update_progress = json_object_get_int_member(json_device, "updateProgress");

	return TRUE;
}

/* async callback handler : read data from sync endpoint continuously */
static void
fu_logitech_bulkcontroller_device_sync_cb(GObject *source_object,
					  GAsyncResult *res,
					  gpointer user_data)
{
	FuLogitechBulkcontrollerHelper *helper = (FuLogitechBulkcontrollerHelper *)user_data;
	FuLogitechBulkcontrollerDevice *self = helper->self;
	guint32 cmd_tmp = 0x0;
	guint64 cmd_tmp_64 = 0x0;
	guint64 cmd_res = 0x0;
	guint32 response_length = 0;
	guint8 ack_payload[SYNC_ACK_PAYLOAD_LENGTH] = {0};
	g_autoptr(GByteArray) buf_ack = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	if (!g_usb_device_bulk_transfer_finish(G_USB_DEVICE(source_object), res, &error_local)) {
		g_propagate_prefixed_error(&helper->error,
					   g_steal_pointer(&error_local),
					   "failed to finish using bulk transfer: ");
		g_main_loop_quit(helper->loop);
		return;
	}
	if (!fu_memread_uint32_safe(helper->buf_pkt->data,
				    helper->buf_pkt->len,
				    COMMAND_OFFSET,
				    &cmd_tmp,
				    G_LITTLE_ENDIAN,
				    &helper->error)) {
		g_prefix_error(&helper->error, "failed to retrieve payload command: ");
		g_main_loop_quit(helper->loop);
		return;
	}
	if (!fu_memread_uint32_safe(helper->buf_pkt->data,
				    helper->buf_pkt->len,
				    LENGTH_OFFSET,
				    &response_length,
				    G_LITTLE_ENDIAN,
				    &helper->error)) {
		g_prefix_error(&helper->error, "failed to retrieve payload length: ");
		g_main_loop_quit(helper->loop);
		return;
	}
	if (!fu_memread_uint64_safe(helper->buf_pkt->data,
				    helper->buf_pkt->len,
				    SYNC_PACKET_HEADER_SIZE,
				    &cmd_tmp_64,
				    G_LITTLE_ENDIAN,
				    &helper->error)) {
		g_prefix_error(&helper->error, "failed to retrieve payload data: ");
		g_main_loop_quit(helper->loop);
		return;
	}
	if (!fu_memcpy_safe((guint8 *)ack_payload,
			    sizeof(ack_payload),
			    0x0,
			    (guint8 *)&cmd_tmp_64,
			    sizeof(cmd_tmp_64),
			    0x0,
			    SYNC_ACK_PAYLOAD_LENGTH,
			    &helper->error)) {
		g_prefix_error(&helper->error, "failed to copy payload data: ");
		g_main_loop_quit(helper->loop);
		return;
	}

	if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL)
		g_debug("Received 0x%x message on sync interface", cmd_tmp);
	switch (cmd_tmp) {
	case CMD_ACK:
		if (!fu_strtoull((const char *)ack_payload,
				 &cmd_res,
				 0,
				 G_MAXUINT32,
				 &error_local)) {
			g_propagate_prefixed_error(&helper->error,
						   g_steal_pointer(&error_local),
						   "failed to parse ack payload cmd: ");
			g_main_loop_quit(helper->loop);
			return;
		}
		if (cmd_res == CMD_BUFFER_WRITE) {
			if (!fu_logitech_bulkcontroller_device_send_sync_cmd(self,
									     CMD_UNINIT_BUFFER,
									     NULL,
									     &helper->error)) {
				g_prefix_error(&helper->error,
					       "failed to send %d while processing %d: ",
					       CMD_UNINIT_BUFFER,
					       CMD_BUFFER_WRITE);
				g_main_loop_quit(helper->loop);
				return;
			}
		} else if (cmd_res != CMD_UNINIT_BUFFER) {
			g_set_error(&helper->error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid message received: expected %s, but received %d: ",
				    (const gchar *)ack_payload,
				    CMD_UNINIT_BUFFER);
			g_main_loop_quit(helper->loop);
			return;
		}
		break;
	case CMD_BUFFER_READ:
		g_byte_array_append(helper->device_response,
				    helper->buf_pkt->data + SYNC_PACKET_HEADER_SIZE,
				    response_length);
		if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
			g_autofree gchar *strsafe =
			    fu_strsafe((const gchar *)helper->device_response->data,
				       helper->device_response->len);
			g_debug("Received data on sync interface. length: %u, buffer: %s",
				helper->device_response->len,
				strsafe);
		}
		fu_byte_array_append_uint32(buf_ack, cmd_tmp, G_LITTLE_ENDIAN);
		if (!fu_logitech_bulkcontroller_device_send_sync_cmd(self,
								     CMD_ACK,
								     buf_ack,
								     &helper->error)) {
			g_prefix_error(&helper->error,
				       "failed to send %d while processing %d: ",
				       CMD_ACK,
				       CMD_BUFFER_READ);
			g_main_loop_quit(helper->loop);
			return;
		}
		break;
	case CMD_UNINIT_BUFFER:
		fu_byte_array_append_uint32(buf_ack, cmd_tmp, G_LITTLE_ENDIAN);
		if (!fu_logitech_bulkcontroller_device_send_sync_cmd(self,
								     CMD_ACK,
								     buf_ack,
								     &helper->error)) {
			g_prefix_error(&helper->error,
				       "failed to send %d while processing %d: ",
				       CMD_ACK,
				       CMD_UNINIT_BUFFER);
			g_main_loop_quit(helper->loop);
			return;
		}
		self->is_sync_transfer_in_progress = FALSE;
		break;
	default:
		break;
	}

	g_main_loop_quit(helper->loop);
	return;
}

static gboolean
fu_logitech_bulkcontroller_device_startlistening_sync(FuLogitechBulkcontrollerDevice *self,
						      GByteArray *device_response,
						      GError **error)
{
	gint max_retry = MAX_RETRIES * 2;
	self->is_sync_transfer_in_progress = TRUE;

	while (self->is_sync_transfer_in_progress) {
		g_autoptr(FuLogitechBulkcontrollerHelper) helper =
		    g_slice_new0(FuLogitechBulkcontrollerHelper);
		max_retry--;
		helper->self = self;
		helper->buf_pkt = g_byte_array_new();
		helper->loop = g_main_loop_new(NULL, FALSE);
		helper->device_response = g_byte_array_ref(device_response);

		fu_byte_array_set_size(helper->buf_pkt, MAX_DATA_SIZE, 0x00);
		g_usb_device_bulk_transfer_async(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
						 self->sync_ep[EP_IN],
						 helper->buf_pkt->data,
						 helper->buf_pkt->len,
						 BULK_TRANSFER_TIMEOUT,
						 NULL, /* cancellable */
						 fu_logitech_bulkcontroller_device_sync_cb,
						 helper);
		g_main_loop_run(helper->loop);

		/* handle error scenario, e.g. device no longer responding */
		if (max_retry == 0) {
			self->is_sync_transfer_in_progress = FALSE;
			if (helper->error != NULL) {
				g_propagate_prefixed_error(error,
							   g_steal_pointer(&helper->error),
							   "failed after %i retries: ",
							   MAX_RETRIES);
			} else {
				g_set_error(&helper->error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "failed after %i retries: ",
					    MAX_RETRIES);
			}
			return FALSE;
		}

		/* just show to console */
		if (helper->error != NULL)
			g_warning("async error %s", helper->error->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_get_data(FuDevice *device, gboolean send_req, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autoptr(GByteArray) decoded_pkt = g_byte_array_new();
	g_autoptr(GByteArray) device_response = g_byte_array_new();
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;

	/* sending GetDeviceInfoRequest. Device reports quite a few matrix, including status,
	 * progress etc
	 * Two ways to get data from device:
	 * 1. Listen for the data broadcasted by device, while firmware upgrade is going on
	 * 2. Make explicit request to the device. Used when data is needed before/after firmware
	 * upgrade
	 */
	if (send_req) {
		g_autoptr(GByteArray) device_request = g_byte_array_new();
		device_request = proto_manager_generate_get_device_info_request();
		if (!fu_logitech_bulkcontroller_device_send_sync_cmd(self,
								     CMD_BUFFER_WRITE,
								     device_request,
								     error)) {
			g_prefix_error(
			    error,
			    "failed to send write buffer packet for device info request: ");
			return FALSE;
		}
	}
	if (!fu_logitech_bulkcontroller_device_startlistening_sync(self, device_response, error)) {
		g_prefix_error(error, "failed to receive data packet for device info request: ");
		return FALSE;
	}
	/* handle error scenario, e.g. CMD_UNINIT_BUFFER arrived before CMD_BUFFER_READ */
	if (device_response->len == 0) {
		g_prefix_error(error,
			       "failed to receive expected packet for device info request: ");
		return FALSE;
	}
	decoded_pkt = proto_manager_decode_message(device_response->data,
						   device_response->len,
						   &proto_id,
						   error);
	if (decoded_pkt == NULL) {
		g_prefix_error(error, "failed to unpack packet for device info request: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
		g_autofree gchar *strsafe =
		    fu_strsafe((const gchar *)decoded_pkt->data, decoded_pkt->len);
		g_debug("Received device response: id: %u, length %u, data: %s",
			proto_id,
			device_response->len,
			strsafe);
	}
	if (proto_id != kProtoId_GetDeviceInfoResponse && proto_id != kProtoId_KongEvent) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "incorrect response for device info request");
		return FALSE;
	}
	if (!fu_logitech_bulkcontroller_device_json_parser(device, decoded_pkt, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_send_upd_init_cmd_cb(FuDevice *device,
						       gpointer user_data,
						       GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	return fu_logitech_bulkcontroller_device_send_upd_cmd(self, CMD_INIT, NULL, error);
}

static gboolean
fu_logitech_bulkcontroller_device_write_fw(FuLogitechBulkcontrollerDevice *self,
					   GBytes *fw,
					   FuProgress *progress,
					   GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, PAYLOAD_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) data_pkt = g_byte_array_new();
		g_byte_array_append(data_pkt, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		if (!fu_logitech_bulkcontroller_device_send_upd_cmd(self,
								    CMD_DATA_TRANSFER,
								    data_pkt,
								    error)) {
			g_prefix_error(error, "failed to send data packet 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_write_firmware(FuDevice *device,
						 FuFirmware *firmware,
						 FuProgress *progress,
						 FwupdInstallFlags flags,
						 GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	gboolean query_device = FALSE;	/* query or listen for events, periodically broadcasted */
	gint max_wait = MAX_WAIT_COUNT; /* if firmware upgrade is taking forever to finish */
	guint max_no_response_count = MAX_RETRIES; /* device doesn't respond */
	guint no_response_count = 0;
	g_autofree gchar *base64hash = NULL;
	g_autoptr(GByteArray) end_pkt = g_byte_array_new();
	g_autoptr(GByteArray) start_pkt = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;
	g_autofree gchar *old_firmware_version = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 48, "device-write-blocks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "end-transfer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "uninit");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 49, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* sending INIT. Retry if device is not in IDLE state to receive the file */
	if (!fu_device_retry(device,
			     fu_logitech_bulkcontroller_device_send_upd_init_cmd_cb,
			     MAX_RETRIES,
			     NULL,
			     error)) {
		g_prefix_error(error,
			       "failed to write init transfer packet: please reboot the device: ");
		return FALSE;
	}

	/* transfer sent */
	fu_byte_array_append_uint64(start_pkt, g_bytes_get_size(fw), G_LITTLE_ENDIAN);
	if (!fu_logitech_bulkcontroller_device_send_upd_cmd(self,
							    CMD_START_TRANSFER,
							    start_pkt,
							    error)) {
		g_prefix_error(error, "failed to write start transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* push each block to device */
	if (!fu_logitech_bulkcontroller_device_write_fw(self,
							fw,
							fu_progress_get_child(progress),
							error))
		return FALSE;
	fu_progress_step_done(progress);

	/* sending end transfer */
	base64hash = fu_logitech_bulkcontroller_device_compute_hash(fw);
	fu_byte_array_append_uint32(end_pkt, 1, G_LITTLE_ENDIAN);   /* update */
	fu_byte_array_append_uint32(end_pkt, 0, G_LITTLE_ENDIAN);   /* force */
	fu_byte_array_append_uint32(end_pkt, MD5, G_LITTLE_ENDIAN); /* checksum type */
	g_byte_array_append(end_pkt, (const guint8 *)base64hash, strlen(base64hash));
	if (!fu_logitech_bulkcontroller_device_send_upd_cmd(self,
							    CMD_END_TRANSFER,
							    end_pkt,
							    error)) {
		g_prefix_error(error, "failed to write end transfer transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send uninit */
	if (!fu_logitech_bulkcontroller_device_send_upd_cmd(self, CMD_UNINIT, NULL, error)) {
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
	g_usleep(G_TIME_SPAN_SECOND);
	/* save the current firmware version for troubleshooting purpose */
	old_firmware_version = g_strdup(fu_device_get_version(device));
	do {
		g_autoptr(GError) error_local = NULL;
		/* skip explicit device query as long as device is publishing update events
		 * (kProtoId_KongEvent) */
		if (self->update_progress == 100) {
			query_device = TRUE;
		} else {
			query_device = (no_response_count == 0) ? FALSE : TRUE;
		}
		g_usleep(500 * G_TIME_SPAN_MILLISECOND);

		/* lost Success/Failure message, device rebooting */
		if (no_response_count == max_no_response_count) {
			g_debug("device not responding, rebooting...");
			break;
		}

		/* update device obj with latest info from the device */
		if (!fu_logitech_bulkcontroller_device_get_data(device,
								query_device,
								&error_local)) {
			no_response_count++;
			g_debug("no response for device info request %u", no_response_count);
			fu_progress_reset(fu_progress_get_child(progress));
			continue;
		}

		/* device responsive, no error and not rebooting yet */
		no_response_count = 0;
		if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
			g_debug("firmware update status: %s. progress: %u",
				fu_logitech_bulkcontroller_device_update_state_to_string(
				    self->update_status),
				self->update_progress);
		}

		/* existing device image version is same as newly pushed image */
		if (self->update_status == kUpdateStateError) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "firmware upgrade failed");
			return FALSE;
		}
		if (self->update_status == kUpdateStateCurrent) {
			if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
				g_debug("new firmware version: %s, old firmware version: %s, "
					"rebooting...",
					fu_device_get_version(device),
					old_firmware_version);
			}
			break;
		}
		if (self->update_progress == 100) {
			/* wait for state change: kUpdateStateUpdating->kUpdateStateCurrent
			 * device no longer broadcast fu related events, need to query device
			 * explicitly now
			 */
			g_usleep(G_USEC_PER_SEC);
			fu_progress_reset(fu_progress_get_child(progress));
			continue;
		}

		/* only update the child if the percentage is bigger -- which means the progressbar
		 * may stall, but will never go backwards */
		if (self->update_progress >
		    fu_progress_get_percentage(fu_progress_get_child(progress))) {
			fu_progress_set_percentage(fu_progress_get_child(progress),
						   self->update_progress);
		}
	} while (max_wait--);
	if (max_wait <= 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "firmware upgrade timeout: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_get_handshake_cb(FuDevice *device,
						   gpointer user_data,
						   GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;
	g_autoptr(GByteArray) decoded_pkt = g_byte_array_new();
	g_autoptr(GByteArray) device_response = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	if (!fu_logitech_bulkcontroller_device_startlistening_sync(self,
								   device_response,
								   &error_local)) {
		if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL)
			g_debug("failed to receive data packet for handshake request");
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to receive data packet for handshake request");
		return FALSE;
	}

	/* handle error scenario, e.g. CMD_UNINIT_BUFFER arrived before CMD_BUFFER_READ */
	if (device_response->len == 0) {
		if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL)
			g_debug("failed to receive expected packet for handshake request");
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to receive expected packet for handshake request");
		return FALSE;
	}

	decoded_pkt = proto_manager_decode_message(device_response->data,
						   device_response->len,
						   &proto_id,
						   &error_local);
	if (decoded_pkt == NULL) {
		if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL)
			g_debug("failed to unpack packet for handshake request");
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to unpack packet for handshake request");
		return FALSE;
	}

	if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
		g_autofree gchar *strsafe =
		    fu_strsafe((const gchar *)decoded_pkt->data, decoded_pkt->len);
		g_debug("Received initialization response: id: %u, length %u, data: %s",
			proto_id,
			device_response->len,
			strsafe);
	}

	/* skip optional initialization events -- not an error if these events are missed */
	if (proto_id != kProtoId_HandshakeEvent) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid initialization message received: %u",
			    proto_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_set_time(FuDevice *device, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autoptr(GByteArray) device_request = g_byte_array_new();
	g_autoptr(GByteArray) decoded_pkt = g_byte_array_new();
	g_autoptr(GByteArray) device_response = g_byte_array_new();
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;

	/* send SetDeviceTimeRequest to sync device clock with host */
	device_request = proto_manager_generate_set_device_time_request();
	if (!fu_logitech_bulkcontroller_device_send_sync_cmd(self,
							     CMD_BUFFER_WRITE,
							     device_request,
							     error)) {
		g_prefix_error(error,
			       "failed to send write buffer packet for set device time request: ");
		return FALSE;
	}
	if (!fu_logitech_bulkcontroller_device_startlistening_sync(self, device_response, error)) {
		g_prefix_error(error,
			       "failed to receive data packet for set device time request: ");
		return FALSE;
	}
	/* handle error scenario, e.g. CMD_UNINIT_BUFFER arrived before CMD_BUFFER_READ */
	if (device_response->len == 0) {
		g_prefix_error(error,
			       "failed to receive expected packet for set device time request: ");
		return FALSE;
	}
	decoded_pkt = proto_manager_decode_message(device_response->data,
						   device_response->len,
						   &proto_id,
						   error);
	if (decoded_pkt == NULL) {
		g_prefix_error(error, "failed to unpack packet for set device time request: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
		g_autofree gchar *strsafe =
		    fu_strsafe((const gchar *)decoded_pkt->data, decoded_pkt->len);
		g_debug("Received device response while processing set device time request: id: "
			"%u, length %u, data: %s",
			proto_id,
			device_response->len,
			strsafe);
	}
	if (proto_id != kProtoId_Ack) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "incorrect response for set device time request");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_bulkcontroller_device_setup(FuDevice *device, GError **error)
{
	FuLogitechBulkcontrollerDevice *self = FU_LOGITECH_BULKCONTROLLER_DEVICE(device);
	g_autoptr(GByteArray) device_request = g_byte_array_new();
	g_autoptr(GByteArray) decoded_pkt = g_byte_array_new();
	g_autoptr(GByteArray) device_response = g_byte_array_new();
	FuLogitechBulkcontrollerProtoId proto_id = kProtoId_UnknownId;
	guint32 success = 0;
	guint32 error_code = 0;
	g_autoptr(GError) error_local = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_bulkcontroller_device_parent_class)->setup(device, error))
		return FALSE;

	/* check for initialization events generated by the device
	 * no error check needed here, possibly missed */
	if (!fu_device_retry(device,
			     fu_logitech_bulkcontroller_device_get_handshake_cb,
			     MAX_HANDSHAKE_RETRIES,
			     NULL,
			     &error_local)) {
		g_warning("failed to receive initialization events: %s", error_local->message);
	}

	/*
	 * device supports USB_Device mode, Appliance mode and BYOD mode.
	 * Only USB_Device mode is supported here.
	 * Ensure it is running in USB_Device mode
	 * Response has two data: Request succeeded or failed, and error code in case of failure
	 */
	device_request = proto_manager_generate_transition_to_device_mode_request();
	if (!fu_logitech_bulkcontroller_device_send_sync_cmd(self,
							     CMD_BUFFER_WRITE,
							     device_request,
							     error)) {
		g_prefix_error(error,
			       "failed to send buffer write packet for transition mode request: ");
		return FALSE;
	}
	if (!fu_logitech_bulkcontroller_device_startlistening_sync(self, device_response, error)) {
		g_prefix_error(error,
			       "failed to receive data packet for transition mode request: ");
		return FALSE;
	}

	/* handle error scenario, e.g. CMD_UNINIT_BUFFER arrived before CMD_BUFFER_READ */
	if (device_response->len == 0) {
		g_prefix_error(error,
			       "failed to receive expected packet for transition mode request: ");
		return FALSE;
	}
	decoded_pkt = proto_manager_decode_message(device_response->data,
						   device_response->len,
						   &proto_id,
						   error);
	if (decoded_pkt == NULL) {
		g_prefix_error(error, "failed to unpack packet for transition mode request: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
		g_autofree gchar *strsafe =
		    fu_strsafe((const gchar *)decoded_pkt->data, decoded_pkt->len);
		g_debug("Received transition mode response: id: %u, length %u, data: %s",
			proto_id,
			device_response->len,
			strsafe);
	}
	if (proto_id != kProtoId_TransitionToDeviceModeResponse) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "incorrect response for transition mode request");
		return FALSE;
	}
	if (!fu_memread_uint32_safe(decoded_pkt->data,
				    decoded_pkt->len,
				    COMMAND_OFFSET,
				    &success,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to retrieve result for transition mode request: ");
		return FALSE;
	}
	if (!fu_memread_uint32_safe(decoded_pkt->data,
				    decoded_pkt->len,
				    LENGTH_OFFSET,
				    &error_code,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error,
			       "failed to retrieve error code for transition mode request: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_LOGITECH_BULKCONTROLLER_VERBOSE") != NULL) {
		g_debug("Received transition mode response. Success: %u, Error: %u",
			success,
			error_code);
	}
	if (!success) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "transition mode request failed. error: %u",
			    error_code);
		return FALSE;
	}

	/* set device time */
	if (!fu_logitech_bulkcontroller_device_set_time(device, error))
		return FALSE;

	/* load current device data */
	if (!fu_logitech_bulkcontroller_device_get_data(device, TRUE, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_logitech_bulkcontroller_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_logitech_bulkcontroller_device_init(FuLogitechBulkcontrollerDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.proto");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_set_remove_delay(FU_DEVICE(self), 100000); /* >1 min to finish init */
}

static void
fu_logitech_bulkcontroller_device_class_init(FuLogitechBulkcontrollerDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_logitech_bulkcontroller_device_to_string;
	klass_device->write_firmware = fu_logitech_bulkcontroller_device_write_firmware;
	klass_device->probe = fu_logitech_bulkcontroller_device_probe;
	klass_device->setup = fu_logitech_bulkcontroller_device_setup;
	klass_device->set_progress = fu_logitech_bulkcontroller_device_set_progress;
}
