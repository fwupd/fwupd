/*
 * Copyright 2021 Quectel Wireless Solutions Co., Ltd.
 *                    Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/usb/ch9.h>
#include <string.h>

#include "fu-sahara-loader.h"
#include "fu-sahara-struct.h"

#define FU_SAHARA_RAW_BUFFER_SIZE (4 * 1024)

#define IO_TIMEOUT_MS 15000

struct _FuSaharaLoader {
	GObject parent_instance;

	FuUsbDevice *usb_device;

	int ep_in;
	int ep_out;
	gsize maxpktsize_in;
	gsize maxpktsize_out;
	gboolean supports_zlp;
};

G_DEFINE_TYPE(FuSaharaLoader, fu_sahara_loader, G_TYPE_OBJECT)

/* IO functions */
static gboolean
fu_sahara_loader_find_interface(FuSaharaLoader *self, FuUsbDevice *usb_device, GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;

	/* all sahara devices use the same vid:pid pair */
	if (fu_device_get_vid(FU_DEVICE(usb_device)) != 0x05c6 ||
	    fu_device_get_pid(FU_DEVICE(usb_device)) != 0x9008) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong device and/or vendor id: 0x%04x 0x%04x",
			    fu_device_get_vid(FU_DEVICE(usb_device)),
			    fu_device_get_pid(FU_DEVICE(usb_device)));
		return FALSE;
	}

	/* parse usb interfaces and find suitable endpoints */
	intfs = fu_usb_device_get_interfaces(usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		FuUsbEndpoint *ep;
		g_autoptr(GPtrArray) endpoints = NULL;

		if (fu_usb_interface_get_class(intf) != 0xFF)
			continue;
		if (fu_usb_interface_get_subclass(intf) != 0xFF)
			continue;
		if (fu_usb_interface_get_protocol(intf) != 0xFF)
			continue;

		endpoints = fu_usb_interface_get_endpoints(intf);
		if (endpoints == NULL || endpoints->len == 0)
			continue;

		for (guint j = 0; j < endpoints->len; j++) {
			ep = g_ptr_array_index(endpoints, j);
			if (fu_usb_endpoint_get_direction(ep) == FU_USB_DIRECTION_DEVICE_TO_HOST) {
				self->ep_in = fu_usb_endpoint_get_address(ep);
				self->maxpktsize_in = fu_usb_endpoint_get_maximum_packet_size(ep);
			} else {
				self->ep_out = fu_usb_endpoint_get_address(ep);
				self->maxpktsize_out = fu_usb_endpoint_get_maximum_packet_size(ep);
			}
		}

		fu_usb_device_add_interface(usb_device, fu_usb_interface_get_number(intf));

		return TRUE;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
}

gboolean
fu_sahara_loader_open(FuSaharaLoader *self, FuUsbDevice *usb_device, GError **error)
{
	if (!fu_sahara_loader_find_interface(self, usb_device, error))
		return FALSE;
	if (!fu_device_open(FU_DEVICE(usb_device), error))
		return FALSE;

	self->usb_device = g_object_ref(usb_device);

	return TRUE;
}

gboolean
fu_sahara_loader_close(FuSaharaLoader *self, GError **error)
{
	if (!self->usb_device) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "usb device interface was not found");
		return FALSE;
	}
	if (!fu_device_close(FU_DEVICE(self->usb_device), error))
		return FALSE;
	g_clear_object(&self->usb_device);
	return TRUE;
}

gboolean
fu_sahara_loader_qdl_is_open(FuSaharaLoader *self)
{
	if (self == NULL)
		return FALSE;
	return fu_device_has_private_flag(FU_DEVICE(self->usb_device),
					  FU_DEVICE_PRIVATE_FLAG_IS_OPEN);
}

GByteArray *
fu_sahara_loader_qdl_read(FuSaharaLoader *self, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_sized_new(FU_SAHARA_RAW_BUFFER_SIZE);
	fu_byte_array_set_size(buf, FU_SAHARA_RAW_BUFFER_SIZE, 0x00);

	if (!fu_usb_device_bulk_transfer(self->usb_device,
					 self->ep_in,
					 buf->data,
					 buf->len,
					 &actual_len,
					 IO_TIMEOUT_MS,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to do bulk transfer (read): ");
		return NULL;
	}

	g_byte_array_set_size(buf, actual_len);
	fu_dump_raw(G_LOG_DOMAIN, "rx packet", buf->data, buf->len);

	return g_steal_pointer(&buf);
}

static gboolean
fu_sahara_loader_qdl_write(FuSaharaLoader *self, const guint8 *data, gsize sz, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) bytes = NULL;

	/* copy const data to mutable GByteArray */
	bytes = g_byte_array_sized_new(sz);
	bytes = g_byte_array_append(bytes, data, sz);
	chunks = fu_chunk_array_mutable_new(bytes->data, bytes->len, 0, 0, self->maxpktsize_out);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_usb_device_bulk_transfer(self->usb_device,
						 self->ep_out,
						 fu_chunk_get_data_out(chk),
						 fu_chunk_get_data_sz(chk),
						 &actual_len,
						 IO_TIMEOUT_MS,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to do bulk transfer (write data): ");
			return FALSE;
		}
		if (actual_len != fu_chunk_get_data_sz(chk)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "only wrote %" G_GSIZE_FORMAT "bytes",
				    actual_len);
			return FALSE;
		}
	}
	if (self->supports_zlp && sz % self->maxpktsize_out == 0) {
		/* sent zlp packet if needed */
		if (!fu_usb_device_bulk_transfer(self->usb_device,
						 self->ep_out,
						 NULL,
						 0,
						 NULL,
						 IO_TIMEOUT_MS,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to do bulk transfer (write zlp): ");
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
fu_sahara_loader_qdl_write_bytes(FuSaharaLoader *self, GBytes *bytes, GError **error)
{
	gsize sz;
	const guint8 *data = g_bytes_get_data(bytes, &sz);
	return fu_sahara_loader_qdl_write(self, data, sz, error);
}

void
fu_sahara_loader_set_supports_zlp(FuSaharaLoader *self, gboolean supports_zlp)
{
	self->supports_zlp = supports_zlp;
}

static gboolean
fu_sahara_loader_write_prog(FuSaharaLoader *self,
			    guint32 offset,
			    guint32 length,
			    GBytes *prog,
			    GError **error)
{
	gsize sz;
	const guint8 *data = g_bytes_get_data(prog, &sz);

	g_return_val_if_fail(offset + length <= sz, FALSE);

	g_debug("SENDING --> RAW_DATA: %u bytes (offset = %u, total = %" G_GSIZE_FORMAT ")",
		length,
		offset,
		sz);
	return fu_sahara_loader_qdl_write(self, &data[offset], length, error);
}

static gboolean
fu_sahara_loader_send_packet(FuSaharaLoader *self, FuStructSaharaPkt *pkt, GError **error)
{
	fu_dump_raw(G_LOG_DOMAIN, "tx packet", pkt->data, pkt->len);
	return fu_sahara_loader_qdl_write(self, pkt->data, pkt->len, error);
}

static gboolean
fu_sahara_loader_send_reset_packet(FuSaharaLoader *self, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructSaharaPktResetReq) st_res = NULL;
	g_autoptr(FuStructSaharaPktResetRes) st_req = fu_struct_sahara_pkt_reset_req_new();

	if (!fu_sahara_loader_send_packet(self, st_req, error)) {
		g_prefix_error(error, "Failed to send reset packet: ");
		return FALSE;
	}

	buf = fu_sahara_loader_qdl_read(self, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_sahara_pkt_reset_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	g_debug("reset succeeded");
	return TRUE;
}

static gboolean
fu_sahara_loader_wait_hello_rsp(FuSaharaLoader *self, GError **error)
{
	g_autoptr(FuStructSaharaPktHelloResponseReq) st_req = NULL;
	g_autoptr(FuStructSaharaPktHelloRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error_local = NULL;

	buf = fu_sahara_loader_qdl_read(self, &error_local);
	if (buf == NULL) {
		g_autoptr(FuStructSaharaPkt) ping = g_byte_array_sized_new(1);
		g_debug("got %s, ignoring with ping", error_local->message);
		g_byte_array_set_size(ping, 1);
		fu_sahara_loader_send_packet(self, ping, NULL);
	}
	if (buf == NULL) {
		buf = fu_sahara_loader_qdl_read(self, error);
		if (buf == NULL)
			return FALSE;
	}
	st_res = fu_struct_sahara_pkt_hello_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	st_req = fu_struct_sahara_pkt_hello_response_req_new();
	return fu_sahara_loader_send_packet(self, st_req, error);
}

/* main routine */
gboolean
fu_sahara_loader_run(FuSaharaLoader *self, GBytes *prog, GError **error)
{
	g_return_val_if_fail(prog != NULL, FALSE);

	g_debug("STATE -- SAHARA_WAIT_HELLO");
	if (!fu_sahara_loader_wait_hello_rsp(self, error))
		return FALSE;

	while (TRUE) {
		guint32 command_id;
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(FuStructSaharaPkt) st_res = NULL;
		g_autoptr(FuStructSaharaPkt) st_req = NULL;
		g_autoptr(GError) error_local = NULL;

		g_debug("STATE -- SAHARA_WAIT_COMMAND");
		buf = fu_sahara_loader_qdl_read(self, error);
		if (buf == NULL)
			break;
		st_res = fu_struct_sahara_pkt_parse(buf->data, buf->len, 0x0, error);
		if (st_res == NULL)
			return FALSE;
		if (buf->len != fu_struct_sahara_pkt_get_hdr_length(st_res)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "received packet length is not matching");
			break;
		}

		command_id = fu_struct_sahara_pkt_get_hdr_command_id(st_res);
		if (command_id == FU_SAHARA_COMMAND_ID_HELLO) {
			st_req = fu_struct_sahara_pkt_hello_response_req_new();
			fu_sahara_loader_send_packet(self, st_req, &error_local);
		} else if (command_id == FU_SAHARA_COMMAND_ID_READ_DATA) {
			g_autoptr(FuStructSaharaPktReadDataRes) st_res2 =
			    fu_struct_sahara_pkt_read_data_res_parse(buf->data,
								     buf->len,
								     0x0,
								     error);
			if (st_res2 == NULL)
				return FALSE;
			fu_sahara_loader_write_prog(
			    self,
			    fu_struct_sahara_pkt_read_data_res_get_offset(st_res2),
			    fu_struct_sahara_pkt_read_data_res_get_length(st_res2),
			    prog,
			    &error_local);
		} else if (command_id == FU_SAHARA_COMMAND_ID_READ_DATA64) {
			g_autoptr(FuStructSaharaPktReadData64Res) st_res2 =
			    fu_struct_sahara_pkt_read_data64_res_parse(buf->data,
								       buf->len,
								       0x0,
								       error);
			if (st_res2 == NULL)
				return FALSE;
			fu_sahara_loader_write_prog(
			    self,
			    fu_struct_sahara_pkt_read_data64_res_get_offset(st_res2),
			    fu_struct_sahara_pkt_read_data64_res_get_length(st_res2),
			    prog,
			    &error_local);
		} else if (command_id == FU_SAHARA_COMMAND_ID_END_OF_IMAGE_TX) {
			g_autoptr(FuStructSaharaPktEndOfImageTxRes) st_res2 =
			    fu_struct_sahara_pkt_end_of_image_tx_res_parse(buf->data,
									   buf->len,
									   0x0,
									   error);
			if (st_res2 == NULL)
				return FALSE;
			if (fu_struct_sahara_pkt_end_of_image_tx_res_get_status(st_res2) ==
			    FU_SAHARA_STATUS_SUCCESS) {
				st_req = fu_struct_sahara_pkt_done_req_new();
				fu_sahara_loader_send_packet(self, st_req, &error_local);
			}
		} else if (command_id == FU_SAHARA_COMMAND_ID_DONE_RESP) {
			return TRUE;
		} else {
			g_warning("Unexpected packet received: cmd_id = %u, len = %u",
				  command_id,
				  fu_struct_sahara_pkt_get_hdr_length(st_res));
		}

		if (error_local != NULL)
			g_warning("%s", error_local->message);
	}

	fu_sahara_loader_send_reset_packet(self, NULL);
	return FALSE;
}

static void
fu_sahara_loader_init(FuSaharaLoader *self)
{
	/* supported by most devices - enable by default */
	self->supports_zlp = TRUE;
}

static void
fu_sahara_loader_finalize(GObject *object)
{
	FuSaharaLoader *self = FU_SAHARA_LOADER(object);
	if (self->usb_device != NULL)
		g_object_unref(self->usb_device);
	G_OBJECT_CLASS(fu_sahara_loader_parent_class)->finalize(object);
}

static void
fu_sahara_loader_class_init(FuSaharaLoaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_sahara_loader_finalize;
}

FuSaharaLoader *
fu_sahara_loader_new(void)
{
	return g_object_new(FU_TYPE_SAHARA_LOADER, NULL);
}
