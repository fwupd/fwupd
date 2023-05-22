/*
 * Copyright (C) 2021 Quectel Wireless Solutions Co., Ltd.
 *                    Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <linux/usb/ch9.h>
#include <string.h>

#include "fu-sahara-loader.h"

#define SAHARA_VERSION		  2
#define SAHARA_VERSION_COMPATIBLE 1

#define SAHARA_RAW_BUFFER_SIZE (4 * 1024)

#define IO_TIMEOUT_MS 15000

struct _FuSaharaLoader {
	GObject parent_instance;

	FuUsbDevice *usb_device;

	int ep_in;
	int ep_out;
	gsize maxpktsize_in;
	gsize maxpktsize_out;
};

G_DEFINE_TYPE(FuSaharaLoader, fu_sahara_loader, G_TYPE_OBJECT)

/* Protocol definitions */
typedef enum {
	SAHARA_NO_CMD_ID = 0,
	SAHARA_HELLO_ID,
	SAHARA_HELLO_RESPONSE_ID,
	SAHARA_READ_DATA_ID,
	SAHARA_END_OF_IMAGE_TX_ID,
	SAHARA_DONE_ID,
	SAHARA_DONE_RESP_ID,
	SAHARA_RESET_ID,
	SAHARA_RESET_RESPONSE_ID,
	SAHARA_READ_DATA_64_BIT_ID = 0x12,
	SAHARA_LAST_CMD_ID
} FuSaharaCommandId;

typedef enum {
	SAHARA_STATUS_SUCCESS = 0,
	SAHARA_STATUS_FAILED,
	SAHARA_STATUS_LAST
} FuSaharaStatusCode;

typedef enum {
	SAHARA_MODE_IMAGE_TX_PENDING,
	SAHARA_MODE_IMAGE_TX_COMPLETE,
	SAHARA_MODE_LAST
} FuSaharaMode;

/* Sahara packet definition */
struct sahara_packet {
	guint32 command_id;
	guint32 length;

	union {
		struct {
			guint32 version;
			guint32 version_compatible;
			guint32 max_packet_length;
			guint32 mode;
		} hello;
		struct {
			guint32 version;
			guint32 version_compatible;
			guint32 status;
			guint32 mode;
			guint32 reserved[6];
		} hello_response;
		struct {
			guint32 image_id;
			guint32 offset;
			guint32 length;
		} read_data;
		struct {
			guint32 image_id;
			guint32 status;
		} end_of_image_transfer;
		/* done packet = header only */
		struct {
			guint32 image_transfer_status;
		} done_response;
		/* reset packet = header only */
		/* reset response packet = header only */
		struct {
			guint64 image_id;
			guint64 offset;
			guint64 length;
		} read_data_64bit;
	};
} __attribute((packed));

/* helper functions */
static FuSaharaCommandId
sahara_packet_get_command_id(GByteArray *packet)
{
	return ((struct sahara_packet *)(packet->data))->command_id;
}

static FuSaharaCommandId
sahara_packet_get_length(GByteArray *packet)
{
	return ((struct sahara_packet *)(packet->data))->length;
}

/* IO functions */
static gboolean
fu_sahara_loader_find_interface(FuSaharaLoader *self, FuUsbDevice *usb_device, GError **error)
{
#ifdef HAVE_GUSB
	g_autoptr(GPtrArray) intfs = NULL;
	GUsbDevice *g_usb_device = fu_usb_device_get_dev(usb_device);

	/* all sahara devices use the same vid:pid pair */
	if (g_usb_device_get_vid(g_usb_device) != 0x05c6 ||
	    g_usb_device_get_pid(g_usb_device) != 0x9008) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Wrong device and/or vendor id: 0x%04x 0x%04x",
			    g_usb_device_get_vid(g_usb_device),
			    g_usb_device_get_pid(g_usb_device));
		return FALSE;
	}

	/* Parse usb interfaces and find suitable endpoints */
	intfs = g_usb_device_get_interfaces(g_usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (g_usb_interface_get_class(intf) == 0xFF &&
		    g_usb_interface_get_subclass(intf) == 0xFF &&
		    g_usb_interface_get_protocol(intf) == 0xFF) {
			GUsbEndpoint *ep;
			g_autoptr(GPtrArray) endpoints = NULL;

			endpoints = g_usb_interface_get_endpoints(intf);
			if (endpoints == NULL || endpoints->len == 0)
				continue;

			for (guint j = 0; j < endpoints->len; j++) {
				ep = g_ptr_array_index(endpoints, j);
				if (g_usb_endpoint_get_direction(ep) ==
				    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST) {
					self->ep_in = g_usb_endpoint_get_address(ep);
					self->maxpktsize_in =
					    g_usb_endpoint_get_maximum_packet_size(ep);
				} else {
					self->ep_out = g_usb_endpoint_get_address(ep);
					self->maxpktsize_out =
					    g_usb_endpoint_get_maximum_packet_size(ep);
				}
			}

			fu_usb_device_add_interface(usb_device, g_usb_interface_get_number(intf));

			return TRUE;
		}
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
#else
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no GUsb support");
	return FALSE;
#endif
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

	return fu_usb_device_is_open(self->usb_device);
}

GByteArray *
fu_sahara_loader_qdl_read(FuSaharaLoader *self, GError **error)
{
#ifdef HAVE_GUSB
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_sized_new(SAHARA_RAW_BUFFER_SIZE);
	fu_byte_array_set_size(buf, SAHARA_RAW_BUFFER_SIZE, 0x00);

	if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(self->usb_device),
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
	g_debug("received %" G_GSIZE_FORMAT " bytes", actual_len);

	return g_steal_pointer(&buf);
#else
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "GUsb not supported");
	return NULL;
#endif
}

static gboolean
fu_sahara_loader_qdl_write(FuSaharaLoader *self, const guint8 *data, gsize sz, GError **error)
{
#ifdef HAVE_GUSB
	gsize actual_len = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) bytes = NULL;

	/* copy const data to mutable GByteArray */
	bytes = g_byte_array_sized_new(sz);
	bytes = g_byte_array_append(bytes, data, sz);
	chunks = fu_chunk_array_mutable_new(bytes->data, bytes->len, 0, 0, self->maxpktsize_out);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(self->usb_device),
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
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "only wrote %" G_GSIZE_FORMAT "bytes",
				    actual_len);
			return FALSE;
		}
	}
	if (sz % self->maxpktsize_out == 0) {
		/* sent zlp packet if needed */
		if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(self->usb_device),
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
#else
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "GUsb not supported");
	return FALSE;
#endif
}

gboolean
fu_sahara_loader_qdl_write_bytes(FuSaharaLoader *self, GBytes *bytes, GError **error)
{
	gsize sz;
	const guint8 *data = g_bytes_get_data(bytes, &sz);
	return fu_sahara_loader_qdl_write(self, data, sz, error);
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
fu_sahara_loader_send_packet(FuSaharaLoader *self, GByteArray *pkt, GError **error)
{
	guint8 *data = pkt->data;
	gsize sz = pkt->len;

	g_return_val_if_fail(pkt != NULL, FALSE);

	fu_dump_raw(G_LOG_DOMAIN, "tx packet", pkt->data, pkt->len);
	return fu_sahara_loader_qdl_write(self, data, sz, error);
}

/* packet composers */
static GByteArray *
fu_sahara_create_byte_array_from_packet(const struct sahara_packet *pkt)
{
	GByteArray *self;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(pkt != NULL, NULL);

	self = g_byte_array_sized_new(pkt->length);
	fu_byte_array_set_size(self, pkt->length, 0x00);
	if (!fu_memcpy_safe(self->data,
			    self->len,
			    0,
			    (gconstpointer)pkt,
			    sizeof(struct sahara_packet),
			    0,
			    pkt->length,
			    &error_local)) {
		g_debug("sahara create packet failed: %s", error_local->message);
		return NULL;
	}

	return self;
}

static GByteArray *
fu_sahara_loader_compose_reset_packet(void)
{
	guint32 len = 0x08;
	struct sahara_packet pkt = {.command_id = GUINT32_TO_LE(SAHARA_RESET_ID),
				    .length = GUINT32_TO_LE(len),
				    {{0}}};

	return fu_sahara_create_byte_array_from_packet(&pkt);
}

static GByteArray *
fu_sahara_loader_compose_hello_response_packet(FuSaharaMode mode)
{
	guint32 len = 0x30;
	struct sahara_packet pkt = {.command_id = GUINT32_TO_LE(SAHARA_HELLO_RESPONSE_ID),
				    .length = GUINT32_TO_LE(len),
				    {{0}}};

	pkt.hello_response.version = GUINT32_TO_LE(SAHARA_VERSION);
	pkt.hello_response.version_compatible = GUINT32_TO_LE(SAHARA_VERSION_COMPATIBLE);
	pkt.hello_response.status = GUINT32_TO_LE(SAHARA_STATUS_SUCCESS);
	pkt.hello_response.mode = GUINT32_TO_LE(SAHARA_MODE_IMAGE_TX_PENDING);

	return fu_sahara_create_byte_array_from_packet(&pkt);
}

static GByteArray *
fu_sahara_loader_compose_done_packet(void)
{
	guint32 len = 0x08;
	struct sahara_packet pkt = {.command_id = GUINT32_TO_LE(SAHARA_DONE_ID),
				    .length = GUINT32_TO_LE(len),
				    {{0}}};

	return fu_sahara_create_byte_array_from_packet(&pkt);
}

static gboolean
fu_sahara_loader_send_reset_packet(FuSaharaLoader *self, GError **error)
{
	g_autoptr(GByteArray) rx_packet = NULL;
	g_autoptr(GByteArray) tx_packet = NULL;

	tx_packet = fu_sahara_loader_compose_reset_packet();
	if (!fu_sahara_loader_send_packet(self, tx_packet, error)) {
		g_prefix_error(error, "Failed to send reset packet: ");
		return FALSE;
	}

	rx_packet = fu_sahara_loader_qdl_read(self, error);
	if (rx_packet == NULL ||
	    sahara_packet_get_command_id(rx_packet) != SAHARA_RESET_RESPONSE_ID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Failed to receive RESET_RESPONSE packet");
		return FALSE;
	}

	g_debug("reset succeeded");
	return TRUE;
}

static gboolean
fu_sahara_loader_wait_hello_rsp(FuSaharaLoader *self, GError **error)
{
	g_autoptr(GByteArray) rx_packet = NULL;
	g_autoptr(GByteArray) tx_packet = NULL;

	rx_packet = fu_sahara_loader_qdl_read(self, error);
	if (rx_packet == NULL) {
		g_autoptr(GByteArray) ping = NULL;
		ping = g_byte_array_sized_new(1);
		g_byte_array_set_size(ping, 1);
		fu_sahara_loader_send_packet(self, ping, NULL);
		rx_packet = fu_sahara_loader_qdl_read(self, error);
	}

	g_return_val_if_fail(rx_packet != NULL, FALSE);

	fu_dump_raw(G_LOG_DOMAIN, "rx packet", rx_packet->data, rx_packet->len);

	if (sahara_packet_get_command_id(rx_packet) != SAHARA_HELLO_ID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Received a different packet while waiting for the HELLO packet");
		fu_sahara_loader_send_reset_packet(self, NULL);
		return FALSE;
	}

	tx_packet = fu_sahara_loader_compose_hello_response_packet(SAHARA_MODE_IMAGE_TX_PENDING);

	return fu_sahara_loader_send_packet(self, tx_packet, error);
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
		struct sahara_packet *pkt;
		g_autoptr(GByteArray) rx_packet = NULL;
		g_autoptr(GByteArray) tx_packet = NULL;
		g_autoptr(GError) error_local = NULL;

		g_debug("STATE -- SAHARA_WAIT_COMMAND");
		rx_packet = fu_sahara_loader_qdl_read(self, error);
		if (rx_packet == NULL)
			break;
		if (rx_packet->len != sahara_packet_get_length(rx_packet)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Received packet length is not matching");
			break;
		}
		fu_dump_raw(G_LOG_DOMAIN, "rx_packet", rx_packet->data, rx_packet->len);

		command_id = sahara_packet_get_command_id(rx_packet);
		pkt = (struct sahara_packet *)(rx_packet->data);
		if (command_id == SAHARA_HELLO_ID) {
			tx_packet = fu_sahara_loader_compose_hello_response_packet(
			    SAHARA_MODE_IMAGE_TX_PENDING);
			fu_sahara_loader_send_packet(self, tx_packet, &error_local);
		} else if (command_id == SAHARA_READ_DATA_ID) {
			guint32 offset = pkt->read_data.offset;
			guint32 length = pkt->read_data.length;
			fu_sahara_loader_write_prog(self, offset, length, prog, &error_local);
		} else if (command_id == SAHARA_READ_DATA_64_BIT_ID) {
			guint64 offset = pkt->read_data_64bit.offset;
			guint64 length = pkt->read_data_64bit.length;
			fu_sahara_loader_write_prog(self, offset, length, prog, &error_local);
		} else if (command_id == SAHARA_END_OF_IMAGE_TX_ID) {
			guint32 status = pkt->end_of_image_transfer.status;
			if (status == SAHARA_STATUS_SUCCESS) {
				tx_packet = fu_sahara_loader_compose_done_packet();
				fu_sahara_loader_send_packet(self, tx_packet, &error_local);
			}
		} else if (command_id == SAHARA_DONE_RESP_ID) {
			return TRUE;
		} else {
			g_warning("Unexpected packet received: cmd_id = %u, len = %u",
				  command_id,
				  sahara_packet_get_length(rx_packet));
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
}

static void
fu_sahara_loader_finalize(GObject *object)
{
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
