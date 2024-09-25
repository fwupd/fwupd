/*
 * Copyright 2024 Pena Christian <christian.a.pena@hp.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "fu-cfu-struct.h"
#include "fu-hpi-cfu-device.h"
#include "fu-hpi-cfu-struct.h"

#define GET_REPORT	   0x01
#define SET_REPORT	   0x09
#define FIRMWARE_REPORT_ID 0x20
#define OFFER_REPORT_ID	   0x25
#define END_POINT_ADDRESS  0x81

#define FU_HPI_CFU_INTERFACE 0x0000
#define IN_REPORT_TYPE	     0x0100
#define OUT_REPORT_TYPE	     0x0200
#define FEATURE_REPORT_TYPE  0x0300

#define FU_HPI_CFU_PAYLOAD_LENGTH 52
#define FU_HPI_CFU_DEVICE_TIMEOUT 0 /* ms */

const guint8 report_data[15] =
    {0x00, 0xff, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

struct _FuHpiCfuDevice {
	FuUsbDevice parent_instance;
	FuHpiCfuState state;
	guint8 force_version;
	guint8 force_reset;
	gint sequence_number;
	gint32 currentaddress;
	gint8 retry_attempts;
	gsize payload_file_size;
	gboolean last_packet_sent;
	guint8 bulk_opt;
	gboolean firmware_status;
	gboolean exit_state_machine_framework;
};

typedef struct {
	FuFirmware *fw_offer;
	FuFirmware *fw_payload;
} FuHpiCfuHandlerOptions;
FuHpiCfuHandlerOptions handler_options;

typedef gint32 (*FuHpiCfuStateHandler)(FuHpiCfuDevice *self,
				       FuProgress *progress,
				       FuHpiCfuHandlerOptions *options,
				       GError **error);

typedef struct {
	FuHpiCfuState state_no;
	FuHpiCfuStateHandler handler;
	FuHpiCfuHandlerOptions *options;
} FuHpiCfuStateMachineFramework;

G_DEFINE_TYPE(FuHpiCfuDevice, fu_hpi_cfu_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_hpi_cfu_device_start_entire_transaction(FuHpiCfuDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GByteArray) st_req = fu_struct_hpi_cfu_buf_new();

	fu_struct_hpi_cfu_buf_set_report_id(st_req, OFFER_REPORT_ID);
	fu_struct_hpi_cfu_buf_set_command(st_req, FU_CFU_OFFER_INFO_CODE_START_ENTIRE_TRANSACTION);
	if (!fu_struct_hpi_cfu_buf_set_report_data(st_req, report_data, sizeof(report_data), error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "StartEntireTransaction", st_req->data, st_req->len);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    SET_REPORT,
					    OUT_REPORT_TYPE | OFFER_REPORT_ID,
					    FU_HPI_CFU_INTERFACE,
					    st_req->data,
					    st_req->len,
					    NULL,
					    FU_HPI_CFU_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_start_entire_transaction_accepted(FuHpiCfuDevice *self, GError **error)
{
	gsize actual_length = 0;
	guint8 buf[128] = {0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      END_POINT_ADDRESS,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      FU_HPI_CFU_DEVICE_TIMEOUT,
					      NULL,
					      &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "BytesReceived", buf, actual_length);
	if (buf[13] == 0x01)
		self->state = FU_HPI_CFU_STATE_START_OFFER_LIST;
	else
		self->state = FU_HPI_CFU_STATE_ERROR;

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_send_start_offer_list(FuHpiCfuDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GByteArray) st_req = fu_struct_hpi_cfu_buf_new();

	fu_struct_hpi_cfu_buf_set_report_id(st_req, OFFER_REPORT_ID);
	fu_struct_hpi_cfu_buf_set_command(st_req, FU_CFU_OFFER_INFO_CODE_START_OFFER_LIST);
	if (!fu_struct_hpi_cfu_buf_set_report_data(st_req, report_data, sizeof(report_data), error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "SendStartOfferList", st_req->data, st_req->len);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    SET_REPORT,
					    OUT_REPORT_TYPE | OFFER_REPORT_ID,
					    FU_HPI_CFU_INTERFACE,
					    st_req->data,
					    st_req->len,
					    NULL,
					    FU_HPI_CFU_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_send_offer_list_accepted(FuHpiCfuDevice *self, gint8 *status, GError **error)
{
	gsize actual_length = 0;
	guint8 buf[128] = {0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      END_POINT_ADDRESS,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      FU_HPI_CFU_DEVICE_TIMEOUT,
					      NULL,
					      &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "SendOfferListAccepted", buf, actual_length);

	/* success */
	if (buf[13] == 0x01) {
		g_debug("success");
	} else {
		if (buf[13] == 0x02) {
			g_warning("accepted with reason: %s", fu_cfu_rr_code_to_string(buf[9]));

		} else {
			g_warning("failed with reason: %s but is not reject.",
				  fu_cfu_rr_code_to_string(buf[9]));
		}
	}
	*status = buf[13];

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_send_offer_update_command(FuHpiCfuDevice *self,
					    FuFirmware *fw_offer,
					    GError **error)
{
	const guint8 *buf;
	gint8 flag_value = 0;
	gsize bufsz = 0;
	g_autoptr(GByteArray) st_req = fu_struct_hpi_cfu_offer_cmd_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GBytes) blob_offer = NULL;

	blob_offer = fu_firmware_get_bytes(fw_offer, error);
	if (blob_offer == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob_offer, &bufsz);

	fu_struct_hpi_cfu_payload_cmd_set_report_id(st_req, OFFER_REPORT_ID);
	if (!fu_memcpy_safe(st_req->data, st_req->len, 0x1, buf, bufsz, 0x0, 16, error))
		return FALSE;

	FU_BIT_SET(flag_value, 7); /* (update now) */
	FU_BIT_SET(flag_value, 6); /* (force update version) */
	fu_struct_hpi_cfu_offer_cmd_set_flags(st_req, flag_value);

	fu_dump_raw(G_LOG_DOMAIN, "SendOfferUpdateCommand", st_req->data, st_req->len);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    SET_REPORT,
					    OUT_REPORT_TYPE | FIRMWARE_REPORT_ID,
					    FU_HPI_CFU_INTERFACE,
					    st_req->data,
					    st_req->len,
					    NULL,
					    FU_HPI_CFU_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_firmware_update_offer_accepted(FuHpiCfuDevice *self,
						 gint8 *reply,
						 gint8 *reason,
						 GError **error)
{
	gsize actual_length = 0;
	guint8 buf[128] = {0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      END_POINT_ADDRESS,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      FU_HPI_CFU_DEVICE_TIMEOUT,
					      NULL,
					      &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "FirmwareUpdateOfferAccepted", buf, actual_length);

	*reason = buf[9];

	if (buf[13] == 0x01) {
		g_debug("success");
	} else {
		if (buf[13] == 0x02) {
			g_debug("offer accepted: %s", fu_cfu_rr_code_to_string(buf[9]));
		} else {
			g_debug("offer accepted: %s is not a reject",
				fu_cfu_rr_code_to_string(buf[9]));
		}
	}

	/* success */
	*reply = buf[13];
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_read_content_ack(FuHpiCfuDevice *self,
				   gboolean *lastpacket,
				   guint8 *report_id,
				   guint8 *reason,
				   guint8 *status,
				   GError **error)
{
	gsize actual_length = 0;
	guint8 buf[128] = {0};
	g_autoptr(GError) error_local = NULL;

	g_debug("sequence number: %d", self->sequence_number);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      END_POINT_ADDRESS,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      FU_HPI_CFU_DEVICE_TIMEOUT,
					      NULL,
					      &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "ReadContentAck", buf, actual_length);

	/* success */
	*report_id = buf[0];
	if (buf[0] == FIRMWARE_REPORT_ID) {
		g_debug("status:%s response:%s",
			fu_cfu_offer_status_to_string(buf[13]),
			fu_cfu_rr_code_to_string(buf[9]));
		if (buf[13] == 0x01) {
			if (self->last_packet_sent)
				*lastpacket = TRUE;
			*status = buf[13];
		}
		*status = buf[13];
	} else {
		g_debug("read_content_ack: buffer[5]: %02x, response:%s",
			(guchar)buf[5],
			fu_cfu_content_status_to_string(buf[5]));

		if (buf[5] == 0x00) {
			g_debug("read_content_ack:1");
			if (self->last_packet_sent)
				*lastpacket = TRUE;
			*status = buf[5];
		} else {
			*status = buf[5];
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_firmware_update_offer_rejected(gint8 reply)
{
	if (reply == FU_HPI_CFU_STATE_UPDATE_OFFER_REJECTED) {
		g_debug("OfferRejected");
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_hpi_cfu_device_send_end_offer_list(FuHpiCfuDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GByteArray) st_req = fu_struct_hpi_cfu_buf_new();

	fu_struct_hpi_cfu_buf_set_report_id(st_req, OFFER_REPORT_ID);
	fu_struct_hpi_cfu_buf_set_command(st_req, FU_CFU_OFFER_INFO_CODE_END_OFFER_LIST);
	if (!fu_struct_hpi_cfu_buf_set_report_data(st_req, report_data, sizeof(report_data), error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "SendEndOfferListCommand", st_req->data, st_req->len);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    SET_REPORT,
					    OUT_REPORT_TYPE | OFFER_REPORT_ID,
					    FU_HPI_CFU_INTERFACE,
					    st_req->data,
					    st_req->len,
					    NULL,
					    FU_HPI_CFU_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_end_offer_list_accepted(FuHpiCfuDevice *self, GError **error)
{
	gsize actual_length = 0;
	guint8 buf[128] = {0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      END_POINT_ADDRESS,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      FU_HPI_CFU_DEVICE_TIMEOUT,
					      NULL,
					      &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "EndOfferListAccepted", buf, actual_length);

	g_debug("identify type 0x%02x", (guchar)buf[4]);
	g_debug("reply status: 0x%02x (%s)", (guchar)buf[13], fu_cfu_rr_code_to_string(buf[13]));

	/* success */
	if (buf[13] != 0x01) {
		if (buf[13] == 0x02) {
			g_warning("not acceptance with reason: %s",
				  fu_cfu_rr_code_to_string(buf[9]));
		} else {
			g_warning("not acceptance with reason: %s but is not REJECT",
				  fu_cfu_rr_code_to_string(buf[9]));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_start_entire_transaction(FuHpiCfuDevice *self,
						   FuProgress *progress,
						   FuHpiCfuHandlerOptions *options,
						   GError **error)
{
	if (!fu_hpi_cfu_device_start_entire_transaction(self, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "start_entire_transaction: ");
		return FALSE;
	}
	self->state = FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION_ACCEPTED;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_start_entire_transaction_accepted(FuHpiCfuDevice *self,
							    FuProgress *progress,
							    FuHpiCfuHandlerOptions *options,
							    GError **error)
{
	if (!fu_hpi_cfu_device_start_entire_transaction_accepted(self, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "start_entire_transaction_accept: ");
		return FALSE;
	}

	self->state = FU_HPI_CFU_STATE_START_OFFER_LIST;
	fu_progress_step_done(progress); /* start-entire  */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_start_offer_list(FuHpiCfuDevice *self,
						FuProgress *progress,
						FuHpiCfuHandlerOptions *options,
						GError **error)
{
	if (!fu_hpi_cfu_device_send_start_offer_list(self, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "start_offer_list: ");
		return FALSE;
	}
	self->state = FU_HPI_CFU_STATE_START_OFFER_LIST_ACCEPTED;

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_start_offer_list_accepted(FuHpiCfuDevice *self,
							 FuProgress *progress,
							 FuHpiCfuHandlerOptions *options,
							 GError **error)
{
	gint8 status = 0;

	if (!fu_hpi_cfu_device_send_offer_list_accepted(self, &status, error)) {
		self->state = FU_HPI_CFU_STATE_UPDATE_STOP;
		g_prefix_error(error, "start_offer_list_accept: ");
		return FALSE;
	}

	if (status >= 0)
		self->state = FU_HPI_CFU_STATE_UPDATE_OFFER;
	else
		self->state = FU_HPI_CFU_STATE_UPDATE_STOP;

	fu_progress_step_done(progress); /* offer-accept  */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_offer_update_command(FuHpiCfuDevice *self,
						    FuProgress *progress,
						    FuHpiCfuHandlerOptions *options,
						    GError **error)
{
	if (!fu_hpi_cfu_device_send_offer_update_command(self, options->fw_offer, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "send_offer_update_command: ");
		return FALSE;
	}
	self->state = FU_HPI_CFU_STATE_UPDATE_OFFER_ACCEPTED;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_offer_accepted(FuHpiCfuDevice *self,
					      FuProgress *progress,
					      FuHpiCfuHandlerOptions *options,
					      GError **error)
{
	gint8 reply = 0;
	gint8 reason = 0;

	if (!fu_hpi_cfu_device_firmware_update_offer_accepted(self, &reply, &reason, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "send_offer_accepted: ");
		return FALSE;
	}
	if (reply == FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_ACCEPT) {
		g_debug("send_offer_accepted: reason: %s",
			fu_hpi_cfu_firmware_update_offer_to_string(reply));
		self->sequence_number = 0;
		self->currentaddress = 0;
		self->last_packet_sent = FALSE;
		self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;
	} else {
		if (reply == FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_SKIP ||
		    reply == FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_REJECT) {
			g_debug("send_offer_accepted: reason: %s",
				fu_hpi_cfu_firmware_update_offer_to_string(reason));
			self->state = FU_HPI_CFU_STATE_UPDATE_MORE_OFFERS;
		} else if (reply == FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_BUSY) {
			g_debug("send_offer_accepted: reason: %s",
				fu_hpi_cfu_firmware_update_offer_to_string(reason));
			self->retry_attempts++;
			self->state = FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION;

			if (self->retry_attempts > 3) {
				self->state = FU_HPI_CFU_STATE_NOTIFY_ON_READY;
				g_warning("send_offer_accepted after 3 retry "
					  "attempts. Restart the device(Reason: Device busy)");
			} else
				self->state = FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION;
		} else {
			self->state = FU_HPI_CFU_STATE_UPDATE_MORE_OFFERS;
		}
	}

	fu_progress_step_done(progress); /* send-offer */

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_send_payload(FuHpiCfuDevice *self, GByteArray *cfu_buf, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_hpi_cfu_payload_cmd_new();
	g_autoptr(GError) error_local = NULL;

	fu_struct_hpi_cfu_payload_cmd_set_report_id(st_req, FIRMWARE_REPORT_ID);

	self->sequence_number++;
	if (self->sequence_number == 1)
		fu_struct_hpi_cfu_payload_cmd_set_flags(st_req, FU_CFU_CONTENT_FLAG_FIRST_BLOCK);
	if (self->last_packet_sent)
		fu_struct_hpi_cfu_payload_cmd_set_flags(st_req, FU_CFU_CONTENT_FLAG_LAST_BLOCK);

	fu_struct_hpi_cfu_payload_cmd_set_length(st_req, cfu_buf->len);
	fu_struct_hpi_cfu_payload_cmd_set_seq_number(st_req, self->sequence_number);
	fu_struct_hpi_cfu_payload_cmd_set_address(st_req, self->currentaddress);

	if (!fu_struct_hpi_cfu_payload_cmd_set_data(st_req, cfu_buf->data, cfu_buf->len, error))
		return FALSE;

	self->currentaddress += cfu_buf->len;

	fu_dump_raw(G_LOG_DOMAIN, "ToDevice", st_req->data, st_req->len);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    SET_REPORT,
					    OUT_REPORT_TYPE | FIRMWARE_REPORT_ID,
					    FU_HPI_CFU_INTERFACE,
					    st_req->data,
					    st_req->len,
					    NULL,
					    FU_HPI_CFU_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_untransmitted_data(GByteArray *payload_data,
				     GByteArray *untransmitted_data,
				     gint8 payload_header_length,
				     guint32 fill_from_position,
				     GError **error)
{
	guint32 remaining_byte_count = payload_header_length - fill_from_position;

	fu_byte_array_set_size(untransmitted_data, remaining_byte_count, 0x00);
	return fu_memcpy_safe(untransmitted_data->data,
			      untransmitted_data->len,
			      0x0,
			      payload_data->data,
			      payload_data->len,
			      fill_from_position,
			      remaining_byte_count,
			      error);
}

static gboolean
fu_hpi_cfu_device_get_payload_header(GByteArray *payload_header,
				     GByteArray *payload_buf,
				     guint32 read_index,
				     GError **error)
{
	fu_byte_array_set_size(payload_header, 5, 0x00);
	return fu_memcpy_safe(payload_header->data,
			      payload_header->len,
			      0x0,
			      payload_buf->data,
			      payload_buf->len,
			      read_index,
			      5,
			      error);
}

static gboolean
fu_hpi_cfu_device_get_payload_data(GByteArray *payload_data,
				   GByteArray *payload_buf,
				   gint8 payload_header_length,
				   gint32 read_index,
				   GError **error)
{
	fu_byte_array_set_size(payload_data, payload_header_length, 0x00);
	return fu_memcpy_safe(payload_data->data,
			      payload_data->len,
			      0x0,
			      payload_buf->data,
			      payload_buf->len,
			      read_index + 5,
			      payload_header_length,
			      error);
}

static gboolean
fu_hpi_cfu_device_send_append_untransmitted(FuHpiCfuDevice *self,
					    gint32 payload_header_length,
					    GByteArray *payload_data,
					    GByteArray *untransmitted_data,
					    GByteArray *cfu_data,
					    GError **error)
{
	gsize remaining_byte_count = 0;
	gsize fill_from_position = 0;

	if (untransmitted_data->len >= FU_HPI_CFU_PAYLOAD_LENGTH) {
		/* cfu_data to be sent to device */
		g_byte_array_append(cfu_data, untransmitted_data->data, FU_HPI_CFU_PAYLOAD_LENGTH);
		if (!fu_hpi_cfu_device_send_payload(self, cfu_data, error))
			return FALSE;

		remaining_byte_count = untransmitted_data->len - FU_HPI_CFU_PAYLOAD_LENGTH;
		fill_from_position = untransmitted_data->len - remaining_byte_count;
		if (remaining_byte_count > 0) {
			g_autoptr(GByteArray) untransmitted_remain = g_byte_array_new();

			/* store the untransmitted_data remaining data */
			if (!fu_hpi_cfu_device_untransmitted_data(untransmitted_data,
								  untransmitted_remain,
								  untransmitted_data->len,
								  fill_from_position,
								  error)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "to set untransmitted_data");
				return FALSE;
			}
		}
	} else {
		/* append untransmitted_data first */
		g_byte_array_append(cfu_data, untransmitted_data->data, untransmitted_data->len);

		fill_from_position = FU_HPI_CFU_PAYLOAD_LENGTH - untransmitted_data->len;
		remaining_byte_count = payload_header_length - fill_from_position;

		/* append actual payload_data */
		g_byte_array_append(cfu_data, payload_data->data, fill_from_position);
		if (!fu_hpi_cfu_device_send_payload(self, cfu_data, error))
			return FALSE;

		if (remaining_byte_count >= FU_HPI_CFU_PAYLOAD_LENGTH) {
			g_autoptr(GByteArray) cfu_data_remain = g_byte_array_new();

			/* append remaining payload_data first */
			g_byte_array_append(cfu_data_remain,
					    payload_data->data + fill_from_position,
					    FU_HPI_CFU_PAYLOAD_LENGTH);
			if (!fu_hpi_cfu_device_send_payload(self, cfu_data_remain, error))
				return FALSE;

			remaining_byte_count = remaining_byte_count - FU_HPI_CFU_PAYLOAD_LENGTH;
			fill_from_position = payload_header_length - remaining_byte_count;
		}

		/* store the untransmitted_data */
		if (!fu_hpi_cfu_device_untransmitted_data(payload_data,
							  untransmitted_data,
							  payload_header_length,
							  fill_from_position,
							  error)) {
			g_prefix_error(error, "failed to set untransmitted_data: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_check_update_content(FuHpiCfuDevice *self,
					       FuProgress *progress,
					       FuHpiCfuHandlerOptions *options,
					       GError **error)
{
	gboolean lastpacket = FALSE;
	gboolean wait_for_burst_ack = FALSE;
	guint8 status = 0;
	guint8 report_id = 0;
	guint8 reason = 0;

	if (self->last_packet_sent) {
		g_debug("check_update_content: last_packet_sent");
		if (!fu_hpi_cfu_device_read_content_ack(self,
							&lastpacket,
							&report_id,
							&reason,
							&status,
							error))
			return FALSE;
	} else {
		switch (self->bulk_opt) {
		case 1:
			if (self->sequence_number % 16 == 0) {
				if (!fu_hpi_cfu_device_read_content_ack(self,
									&lastpacket,
									&report_id,
									&reason,
									&status,
									error))
					return FALSE;
			} else {
				self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;
				wait_for_burst_ack = TRUE;
			}
			break;

		case 2:
			if (self->sequence_number % 32 == 0) {
				if (!fu_hpi_cfu_device_read_content_ack(self,
									&lastpacket,
									&report_id,
									&reason,
									&status,
									error))
					return FALSE;
			} else {
				self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;
				wait_for_burst_ack = TRUE;
			}
			break;

		case 3:
			if ((self->sequence_number) % 64 == 0) {
				if (!fu_hpi_cfu_device_read_content_ack(self,
									&lastpacket,
									&report_id,
									&reason,
									&status,
									error))
					return FALSE;
			} else {
				self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;
				wait_for_burst_ack = TRUE;
			}
			break;

		default:
			if (!fu_hpi_cfu_device_read_content_ack(self,
								&lastpacket,
								&report_id,
								&reason,
								&status,
								error))
				return FALSE;
		}
	}

	if (wait_for_burst_ack)
		return TRUE;

	if (self->last_packet_sent) {
		self->state = FU_HPI_CFU_STATE_UPDATE_SUCCESS;
	} else
		self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;

	if (report_id == 0x25) {
		g_debug("check_update_content: report_id:%d", report_id == FIRMWARE_REPORT_ID);
		switch (status) {
		case FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_SKIP:
		case FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_REJECT:
		case FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_COMMAND_READY:
		case FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_CMD_NOT_SUPPORTED:
			g_warning("check_update_content: reason: %s",
				  fu_hpi_cfu_firmware_update_offer_to_string(status));
			self->state = FU_HPI_CFU_STATE_UPDATE_MORE_OFFERS;
			break;

		case FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_ACCEPT:
			g_debug("check_update_content: reason: %s",
				fu_hpi_cfu_firmware_update_offer_to_string(status));
			if (lastpacket) {
				g_debug("check_update_content: reason: %s for last_packet_sent",
					fu_hpi_cfu_firmware_update_offer_to_string(status));
				self->state = FU_HPI_CFU_STATE_UPDATE_SUCCESS;
			} else
				self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;
			break;

		case FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_BUSY:
			g_warning("check_update_content: reason:%s",
				  fu_hpi_cfu_firmware_update_offer_to_string(status));
			self->state = FU_HPI_CFU_STATE_NOTIFY_ON_READY;
			break;

		default:
			g_warning("check_update_content: FU_HPI_CFU_STATE_ERROR");
			self->state = FU_HPI_CFU_STATE_ERROR;
			break;
		}
	} else if (report_id == 0x22) {
		g_debug("check_update_content: report_id:0x22");
		switch (status) {
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_PREPARE:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_WRITE:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_COMPLETE:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_VERIFY:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_CRC:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_SIGNATURE:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_VERSION:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_SWAP_PENDING:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_INVALID_ADDR:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_NO_OFFER:
		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_ERROR_INVALID:
			self->state = FU_HPI_CFU_STATE_ERROR;
			g_warning("check_update_content: reason:%s",
				  fu_cfu_content_status_to_string(status));
			g_debug("check_update_content: %s",
				fu_hpi_cfu_firmware_update_status_to_string(status));
			break;

		case FU_HPI_CFU_FIRMWARE_UPDATE_STATUS_SUCCESS:
			g_debug("check_update_content: SUCCESS");
			if (lastpacket) {
				self->state = FU_HPI_CFU_STATE_UPDATE_SUCCESS;
			} else
				self->state = FU_HPI_CFU_STATE_UPDATE_CONTENT;
			break;

		default:
			g_warning("check_update_content: status none.");
			self->state = FU_HPI_CFU_STATE_ERROR;
			break;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_payload_chunk(FuHpiCfuDevice *self,
					     FuChunk *chk,
					     FuProgress *progress,
					     FuHpiCfuHandlerOptions *options,
					     GError **error)
{
	g_autoptr(GByteArray) payload_buf = g_byte_array_new();
	g_autoptr(GByteArray) untransmitted_data = g_byte_array_new();

	g_byte_array_append(payload_buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

	for (guint32 read_index = 0; read_index < payload_buf->len;) {
		g_autoptr(GByteArray) payload_header = g_byte_array_new();
		g_autoptr(GByteArray) payload_data = g_byte_array_new();
		g_autoptr(GByteArray) cfu_data = g_byte_array_new();
		gint8 payload_header_length = 0;

		/* payload header */
		if (!fu_hpi_cfu_device_get_payload_header(payload_header,
							  payload_buf,
							  read_index,
							  error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "to get payload header");
			return FALSE;
		}

		payload_header_length = payload_header->data[4];

		/* payload data */
		if (!fu_hpi_cfu_device_get_payload_data(payload_data,
							payload_buf,
							payload_header_length,
							read_index,
							error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "to get payload data");
			return FALSE;
		}

		read_index = read_index + payload_header_length + 5;

		if (untransmitted_data->data != NULL) {
			/* handle untransmitted_data */
			if (!fu_hpi_cfu_device_send_append_untransmitted(self,
									 payload_header_length,
									 payload_data,
									 untransmitted_data,
									 cfu_data,
									 error))
				return FALSE;

			self->last_packet_sent = (read_index >= payload_buf->len) ? TRUE : FALSE;
		}

		else {
			guint32 remaining_byte_count = 0;
			guint32 fill_from_position = 0;

			if (payload_header_length > FU_HPI_CFU_PAYLOAD_LENGTH) {
				/* cfu_data to be sent to device */
				g_byte_array_append(cfu_data,
						    payload_data->data,
						    FU_HPI_CFU_PAYLOAD_LENGTH);

				if (!fu_hpi_cfu_device_send_payload(self, cfu_data, error))
					return FALSE;

				remaining_byte_count =
				    payload_header_length - FU_HPI_CFU_PAYLOAD_LENGTH;
				fill_from_position = payload_header_length - remaining_byte_count;

				/* store the remaining bytes to untransmitted_data */
				if (!fu_hpi_cfu_device_untransmitted_data(payload_data,
									  untransmitted_data,
									  payload_header_length,
									  fill_from_position,
									  error)) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "to set untransmitted_data");
					return FALSE;
				}
			} else {
				self->last_packet_sent = (read_index >= payload_buf->len) ? TRUE
											  : FALSE;

				/* cfu_data to be sent to device */
				g_byte_array_append(cfu_data,
						    payload_data->data,
						    payload_data->len);

				if (!fu_hpi_cfu_device_send_payload(self, cfu_data, error))
					return FALSE;
			}
		}

		if (self->last_packet_sent) {
			if (untransmitted_data->data != NULL) {
				g_autoptr(GByteArray) cfu_last_packet = g_byte_array_new();

				/* clear and assign the latest untransmitted_data */
				fu_byte_array_set_size(cfu_last_packet,
						       untransmitted_data->len,
						       0x00);
				if (!fu_memcpy_safe(cfu_last_packet->data,
						    cfu_last_packet->len,
						    0x0,
						    untransmitted_data->data,
						    untransmitted_data->len,
						    0x0,
						    untransmitted_data->len,
						    error))
					return FALSE;

				g_debug("sending payload last packet");
				if (!fu_hpi_cfu_device_send_payload(self, cfu_last_packet, error))
					return FALSE;
			}
		}

		if (!fu_hpi_cfu_device_handler_check_update_content(self, progress, options, error))
			return FALSE;

		if (self->state != FU_HPI_CFU_STATE_UPDATE_CONTENT)
			break;
	}

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_payload(FuHpiCfuDevice *self,
				       FuProgress *progress,
				       FuHpiCfuHandlerOptions *options,
				       GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_firmware_get_chunks(options->fw_payload, error);
	if (chunks == NULL) {
		g_prefix_error(error, "null chunks");
		return FALSE;
	}
	for (guint32 i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_hpi_cfu_device_handler_send_payload_chunk(self,
								  chk,
								  progress,
								  options,
								  error)) {
			g_prefix_error(error, "send_payload: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_update_success(FuHpiCfuDevice *self,
					 FuProgress *progress,
					 FuHpiCfuHandlerOptions *options,
					 GError **error)
{
	if (self->last_packet_sent) {
		self->firmware_status = TRUE;
		self->state = FU_HPI_CFU_STATE_END_OFFER_LIST;
	} else {
		self->state = FU_HPI_CFU_STATE_UPDATE_MORE_OFFERS;
	}
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_update_offer_rejected(FuHpiCfuDevice *self,
						FuProgress *progress,
						FuHpiCfuHandlerOptions *options,
						GError **error)
{
	if (self->last_packet_sent)
		self->state = FU_HPI_CFU_STATE_END_OFFER_LIST;
	else
		self->state = FU_HPI_CFU_STATE_UPDATE_OFFER;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_update_more_offers(FuHpiCfuDevice *self,
					     FuProgress *progress,
					     FuHpiCfuHandlerOptions *options,
					     GError **error)
{
	if (self->last_packet_sent)
		self->state = FU_HPI_CFU_STATE_END_OFFER_LIST;
	else
		self->state = FU_HPI_CFU_STATE_UPDATE_OFFER;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_send_end_offer_list(FuHpiCfuDevice *self,
					      FuProgress *progress,
					      FuHpiCfuHandlerOptions *options,
					      GError **error)
{
	if (!fu_hpi_cfu_device_send_end_offer_list(self, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "send_end_offer_list: ");
		return FALSE;
	}
	self->state = FU_HPI_CFU_STATE_END_OFFER_LIST_ACCEPTED;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_end_offer_list_accepted(FuHpiCfuDevice *self,
						  FuProgress *progress,
						  FuHpiCfuHandlerOptions *options,
						  GError **error)
{
	if (!fu_hpi_cfu_device_end_offer_list_accepted(self, error)) {
		g_prefix_error(error, "end_offer_list_accept: ");
		return FALSE;
	}
	self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_BY_SENDING_OFFER_LIST_AGAIN;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_update_stop(FuHpiCfuDevice *self,
				      FuProgress *progress,
				      FuHpiCfuHandlerOptions *options,
				      GError **error)
{
	self->exit_state_machine_framework = TRUE;
	fu_progress_step_done(progress); /* send-payload */
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_error(FuHpiCfuDevice *self,
				FuProgress *progress,
				FuHpiCfuHandlerOptions *options,
				GError **error)
{
	self->state = FU_HPI_CFU_STATE_UPDATE_STOP;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_notify_on_ready(FuHpiCfuDevice *self,
					  FuProgress *progress,
					  FuHpiCfuHandlerOptions *options,
					  GError **error)
{
	self->state = FU_HPI_CFU_STATE_WAIT_FOR_READY_NOTIFICATION;

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_wait_for_ready_notification(FuHpiCfuDevice *self,
						      FuProgress *progress,
						      FuHpiCfuHandlerOptions *options,
						      GError **error)
{
	self->state = FU_HPI_CFU_STATE_UPDATE_STOP;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_swap_pending_send_offer_list_again(FuHpiCfuDevice *self,
							     FuProgress *progress,
							     FuHpiCfuHandlerOptions *options,
							     GError **error)
{
	if (!fu_hpi_cfu_device_send_start_offer_list(self, error)) {
		self->state = FU_HPI_CFU_STATE_UPDATE_VERIFY_ERROR;
		g_prefix_error(error, "swap_pending_send_offer_list_again: ");
		return FALSE;
	}

	self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_OFFER_LIST_ACCEPTED;

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_swap_pending_offer_list_accepted(FuHpiCfuDevice *self,
							   FuProgress *progress,
							   FuHpiCfuHandlerOptions *options,
							   GError **error)
{
	gint8 status = 0;

	if (!fu_hpi_cfu_device_send_offer_list_accepted(self, &status, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "swap_pending_offer_list_accept: ");
		return FALSE;
	}

	if (status >= 0)
		self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_SEND_OFFER_AGAIN;
	else
		self->state = FU_HPI_CFU_STATE_UPDATE_VERIFY_ERROR;

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_swap_pending_send_offer_again(FuHpiCfuDevice *self,
							FuProgress *progress,
							FuHpiCfuHandlerOptions *options,
							GError **error)
{
	if (!fu_hpi_cfu_device_send_offer_update_command(self, options->fw_offer, error)) {
		self->state = FU_HPI_CFU_STATE_ERROR;
		g_prefix_error(error, "swap_pending_send_offer_again: ");
		return FALSE;
	}
	self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_OFFER_ACCEPTED;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_swap_pending_send_offer_list_accepted(FuHpiCfuDevice *self,
								FuProgress *progress,
								FuHpiCfuHandlerOptions *options,
								GError **error)
{
	gint8 reply = 0;
	gint8 reason = 0;

	/* reply status must be SWAP_PENDING */
	if (!fu_hpi_cfu_device_firmware_update_offer_accepted(self, &reply, &reason, error)) {
		g_prefix_error(error, "swap_pending_send_offer_accept: ");
		return FALSE;
	}

	if (reply == FU_HPI_CFU_FIRMWARE_UPDATE_OFFER_ACCEPT) {
		g_debug("swap_pending_send_offer_list_accepted: "
			"expected a reject with SWAP PENDING");
		self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_SEND_UPDATE_END_OFFER_LIST;
	} else {
		if (fu_hpi_cfu_device_firmware_update_offer_rejected(reply)) {
			g_debug("swap_pending_send_offer_list_accepted: "
				"reply: %d,OFFER_REJECTED: Reason:'%s'",
				reply,
				fu_cfu_rr_code_to_string(reason));

			switch (reason) {
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_OLD_FW:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_INV_COMPONENT:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_SWAP_PENDING:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_MISMATCH:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_BANK:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_PLATFORM:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_MILESTONE:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_INV_PCOL_REV:
			case FU_HPI_CFU_FIRMWARE_OFFER_REJECT_VARIANT:
				g_debug("reason: %s",
					fu_hpi_cfu_firmware_offer_reject_to_string(reason));
				break;

			default:
				g_debug("swap_pending_send_offer_list_accepted "
					"expected a reject with SWAP PENDING");
				break;
			}
		}
		/* rejected */
		self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_SEND_UPDATE_END_OFFER_LIST;
	}
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_swap_pending_send_end_offer_list(FuHpiCfuDevice *self,
							   FuProgress *progress,
							   FuHpiCfuHandlerOptions *options,
							   GError **error)
{
	if (!fu_hpi_cfu_device_send_end_offer_list(self, error)) {
		g_prefix_error(error, "swap_pending_send_end_offer_list: ");
		return FALSE;
	}

	self->state = FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_UPDATE_END_OFFER_LIST_ACCEPTED;
	fu_progress_step_done(progress); /* send-payload */

	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_swap_pending_end_offer_list_accepted(FuHpiCfuDevice *self,
							       FuProgress *progress,
							       FuHpiCfuHandlerOptions *options,
							       GError **error)
{
	if (!fu_hpi_cfu_device_end_offer_list_accepted(self, error)) {
		g_prefix_error(error, "swap_pending_end_offer_list_accept: ");
		return FALSE;
	}

	self->state = FU_HPI_CFU_STATE_UPDATE_STOP;
	return TRUE;
}

static gboolean
fu_hpi_cfu_device_handler_verify_error(FuHpiCfuDevice *self,
				       FuProgress *progress,
				       FuHpiCfuHandlerOptions *options,
				       GError **error)
{
	self->state = FU_HPI_CFU_STATE_UPDATE_STOP;

	return TRUE;
}

FuHpiCfuStateMachineFramework hpi_cfu_states[] = {
    {FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION,
     fu_hpi_cfu_device_handler_start_entire_transaction,
     NULL},
    {FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION_ACCEPTED,
     fu_hpi_cfu_device_handler_start_entire_transaction_accepted,
     NULL},
    {FU_HPI_CFU_STATE_START_OFFER_LIST, fu_hpi_cfu_device_handler_send_start_offer_list, NULL},
    {FU_HPI_CFU_STATE_START_OFFER_LIST_ACCEPTED,
     fu_hpi_cfu_device_handler_send_start_offer_list_accepted,
     NULL},
    {FU_HPI_CFU_STATE_UPDATE_OFFER,
     fu_hpi_cfu_device_handler_send_offer_update_command,
     &handler_options},
    {FU_HPI_CFU_STATE_UPDATE_OFFER_ACCEPTED, fu_hpi_cfu_device_handler_send_offer_accepted, NULL},
    {FU_HPI_CFU_STATE_UPDATE_CONTENT, fu_hpi_cfu_device_handler_send_payload, &handler_options},
    {FU_HPI_CFU_STATE_UPDATE_SUCCESS, fu_hpi_cfu_device_handler_update_success, NULL},
    {FU_HPI_CFU_STATE_UPDATE_OFFER_REJECTED, fu_hpi_cfu_device_handler_update_offer_rejected, NULL},
    {FU_HPI_CFU_STATE_UPDATE_MORE_OFFERS, fu_hpi_cfu_device_handler_update_more_offers, NULL},
    {FU_HPI_CFU_STATE_END_OFFER_LIST, fu_hpi_cfu_device_handler_send_end_offer_list, NULL},
    {FU_HPI_CFU_STATE_END_OFFER_LIST_ACCEPTED,
     fu_hpi_cfu_device_handler_end_offer_list_accepted,
     NULL},
    {FU_HPI_CFU_STATE_UPDATE_STOP, fu_hpi_cfu_device_handler_update_stop, NULL},
    {FU_HPI_CFU_STATE_ERROR, fu_hpi_cfu_device_handler_error, NULL},
    {FU_HPI_CFU_STATE_CHECK_UPDATE_CONTENT, fu_hpi_cfu_device_handler_check_update_content, NULL},
    {FU_HPI_CFU_STATE_NOTIFY_ON_READY, fu_hpi_cfu_device_handler_notify_on_ready, NULL},
    {FU_HPI_CFU_STATE_WAIT_FOR_READY_NOTIFICATION,
     fu_hpi_cfu_device_handler_wait_for_ready_notification,
     NULL},
    {FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_BY_SENDING_OFFER_LIST_AGAIN,
     fu_hpi_cfu_device_handler_swap_pending_send_offer_list_again,
     NULL},
    {FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_OFFER_LIST_ACCEPTED,
     fu_hpi_cfu_device_handler_swap_pending_offer_list_accepted,
     NULL},
    {FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_SEND_OFFER_AGAIN,
     fu_hpi_cfu_device_handler_swap_pending_send_offer_again,
     &handler_options},
    {FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_OFFER_ACCEPTED,
     fu_hpi_cfu_device_handler_swap_pending_send_offer_list_accepted,
     NULL},
    {FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_SEND_UPDATE_END_OFFER_LIST,
     fu_hpi_cfu_device_handler_swap_pending_send_end_offer_list,
     NULL},
    {FU_HPI_CFU_STATE_VERIFY_CHECK_SWAP_PENDING_UPDATE_END_OFFER_LIST_ACCEPTED,
     fu_hpi_cfu_device_handler_swap_pending_end_offer_list_accepted,
     NULL},
    {FU_HPI_CFU_STATE_UPDATE_VERIFY_ERROR, fu_hpi_cfu_device_handler_verify_error, NULL},
};

static gboolean
fu_hpi_cfu_device_setup(FuDevice *device, GError **error)
{
	guint32 version_raw;
	gint8 version_table_offset = 4;
	gint8 component_id_offset = 5;
	gint8 component_data_size = 8;
	gint8 component_index = 0; /* multiple offers logic is in progress */
	gint8 bulk_opt_index = 0;
	gsize actual_length = 0;
	guint8 buf[60] = {0};

	FuHpiCfuDevice *self = FU_HPI_CFU_DEVICE(device);

	/* FuHidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_hpi_cfu_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    GET_REPORT,
					    FEATURE_REPORT_TYPE | FIRMWARE_REPORT_ID,
					    FU_HPI_CFU_INTERFACE,
					    buf,
					    sizeof(buf),
					    &actual_length,
					    FU_HPI_CFU_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to do device setup");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "VersionResponse", buf, actual_length);
	if (!fu_memread_uint32_safe(buf, sizeof(buf), 5, &version_raw, G_LITTLE_ENDIAN, error))
		return FALSE;
	fu_device_set_version_raw(device, version_raw);

	bulk_opt_index =
	    version_table_offset + component_index * component_data_size + component_id_offset;

	/* Get Bulk optimization value */
	if (!fu_memcpy_safe((guint8 *)&self->bulk_opt,
			    sizeof(self->bulk_opt),
			    0x0,
			    (guint8 *)buf + bulk_opt_index,
			    1,
			    0x0,
			    1,
			    error))
		return FALSE;
	g_debug("bulk_opt: %d", self->bulk_opt);

	/* success */
	return TRUE;
}

static void
fu_hpi_cfu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_percentage(progress, 0);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 4, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 86, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");
}

static gboolean
fu_hpi_cfu_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuHpiCfuDevice *self = FU_HPI_CFU_DEVICE(device);
	gsize payload_file_size = 0;
	g_autoptr(FuFirmware) fw_offer = NULL;
	g_autoptr(FuFirmware) fw_payload = NULL;
	g_autoptr(GBytes) blob_payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "start-entire");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "start-offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "send-offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92, "send-payload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 8, "restart");

	/* get both images */
	fw_offer = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware),
							 "*.offer.bin",
							 error);
	if (fw_offer == NULL)
		return FALSE;
	fw_payload = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware),
							   "*.payload.bin",
							   error);
	if (fw_payload == NULL)
		return FALSE;

	self->state = FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION;
	blob_payload = fu_firmware_get_bytes(fw_payload, NULL);
	g_bytes_get_data(blob_payload, &payload_file_size);
	self->payload_file_size = payload_file_size;
	handler_options.fw_offer = fw_offer;
	handler_options.fw_payload = fw_payload;

	/* cfu state machine framework */
	while (!self->exit_state_machine_framework) {
		g_debug("hpi-cfu-state: %s", fu_hpi_cfu_state_to_string(self->state));
		if (!hpi_cfu_states[self->state].handler(self,
							 progress,
							 hpi_cfu_states[self->state].options,
							 error)) {
			g_prefix_error(error, "state: ");
			return FALSE;
		}
	}

	/* the device automatically reboots */
	if (self->firmware_status)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gchar *
fu_hpi_cfu_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%02x.%02x.%02x.%02x",
			       (guint)(version_raw >> 24) & 0xff,
			       (guint)(version_raw >> 16) & 0xff,
			       (guint)(version_raw >> 8) & 0xff,
			       (guint)version_raw & 0xff);
}

static void
fu_hpi_cfu_device_init(FuHpiCfuDevice *self)
{
	self->state = FU_HPI_CFU_STATE_START_ENTIRE_TRANSACTION;

	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.cfu");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), FU_HPI_CFU_INTERFACE);

	/* reboot takes down the entire hub for ~12 minutes */
	fu_device_set_remove_delay(FU_DEVICE(self), 720 * 1000);
}

static void
fu_hpi_cfu_device_class_init(FuHpiCfuDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_hpi_cfu_device_write_firmware;
	device_class->setup = fu_hpi_cfu_device_setup;
	device_class->set_progress = fu_hpi_cfu_device_set_progress;
	device_class->convert_version = fu_hpi_cfu_device_convert_version;
}
