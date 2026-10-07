/*
 * Copyright 2026 Sayed Kaif <metsw24@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cfu-struct.h"
#include "fu-context-private.h"
#include "fu-hpi-cfu-device.h"
#include "fu-hpi-cfu-struct.h"

static void
fu_test_hpi_cfu_add_event_out(FuHpiCfuDevice *self, guint16 value, GByteArray *buf)
{
	g_autofree gchar *data = fu_base64_encode(buf->data, buf->len);
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuDeviceEvent) event = NULL;

	event_id = g_strdup_printf("ControlTransfer:"
				   "Direction=0x01,"
				   "RequestType=0x02,"
				   "Recipient=0x00,"
				   "Request=0x09,"
				   "Value=0x%04x,"
				   "Idx=0x0000,"
				   "Data=%s,"
				   "Length=0x%x",
				   value,
				   data,
				   buf->len);
	event = fu_device_event_new(event_id);
	fu_device_event_set_data(event, "Data", buf->data, buf->len);
	fu_device_add_event(FU_DEVICE(self), event);
}

static void
fu_test_hpi_cfu_add_event_in(FuHpiCfuDevice *self, gsize offset, guint8 value)
{
	guint8 buf[128] = {0x0};
	g_autofree gchar *data = fu_base64_encode(buf, sizeof(buf));
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuDeviceEvent) event = NULL;

	event_id = g_strdup_printf("InterruptTransfer:Endpoint=0x81,Data=%s,Length=0x80", data);
	event = fu_device_event_new(event_id);
	buf[offset] = value;
	fu_device_event_set_data(event, "Data", buf, sizeof(buf));
	fu_device_add_event(FU_DEVICE(self), event);
}

static void
fu_test_hpi_cfu_add_event_offer_info(FuHpiCfuDevice *self, FuCfuOfferInfoCode code)
{
	const guint8 report_data[15] = {0x00, 0xff, 0xa0};
	gboolean ret;
	g_autoptr(FuStructHpiCfuBuf) st = fu_struct_hpi_cfu_buf_new();
	g_autoptr(GError) error = NULL;

	fu_struct_hpi_cfu_buf_set_report_id(st, 0x25);
	fu_struct_hpi_cfu_buf_set_command(st, code);
	ret = fu_struct_hpi_cfu_buf_set_report_data(st, report_data, sizeof(report_data), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_test_hpi_cfu_add_event_out(self, 0x0225, st->buf);
	fu_test_hpi_cfu_add_event_in(self, 13, 0x01);
}

static void
fu_test_hpi_cfu_add_block(GByteArray *buf, guint32 addr, guint8 len)
{
	fu_byte_array_append_uint32(buf, addr, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, len);
	fu_byte_array_set_size(buf, buf->len + len, 0xAA);
}

/* a block that is too small to fill the packet started by the previous block */
static void
fu_hpi_cfu_device_short_block_func(void)
{
	gboolean ret;
	guint8 buf_offer[16] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_HPI_CFU_DEVICE, "context", ctx, NULL);
	g_autoptr(FuFirmware) firmware = fu_zip_firmware_new();
	g_autoptr(FuFirmware) img_offer = fu_zip_file_new();
	g_autoptr(FuFirmware) img_payload = fu_zip_file_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuStructHpiCfuOfferCmd) st_offer = fu_struct_hpi_cfu_offer_cmd_new();
	g_autoptr(FuStructHpiCfuPayloadCmd) st_payload = fu_struct_hpi_cfu_payload_cmd_new();
	g_autoptr(GByteArray) buf_payload = g_byte_array_new();
	g_autoptr(GBytes) blob_offer = g_bytes_new(buf_offer, sizeof(buf_offer));
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_set_id(img_offer, "test.offer.bin");
	fu_firmware_set_bytes(img_offer, blob_offer);
	ret = fu_firmware_add_image(firmware, img_offer, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* 60 bytes leaves 8 bytes for the next packet, which then needs 44 more */
	fu_test_hpi_cfu_add_block(buf_payload, 0, 60);
	fu_test_hpi_cfu_add_block(buf_payload, 60, 4);
	blob_payload = g_bytes_new(buf_payload->data, buf_payload->len);
	fu_firmware_set_id(img_payload, "test.payload.bin");
	fu_firmware_set_bytes(img_payload, blob_payload);
	ret = fu_firmware_add_image(firmware, img_payload, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* accept everything up to and including the first content packet */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_test_hpi_cfu_add_event_offer_info(FU_HPI_CFU_DEVICE(device),
					     FU_CFU_OFFER_INFO_CODE_START_ENTIRE_TRANSACTION);
	fu_test_hpi_cfu_add_event_offer_info(FU_HPI_CFU_DEVICE(device),
					     FU_CFU_OFFER_INFO_CODE_START_OFFER_LIST);
	fu_struct_hpi_cfu_offer_cmd_set_report_id(st_offer, 0x25);
	fu_struct_hpi_cfu_offer_cmd_set_flags(st_offer, 0xC0);
	fu_test_hpi_cfu_add_event_out(FU_HPI_CFU_DEVICE(device), 0x0220, st_offer->buf);
	fu_test_hpi_cfu_add_event_in(FU_HPI_CFU_DEVICE(device), 13, 0x01);
	fu_struct_hpi_cfu_payload_cmd_set_report_id(st_payload, 0x20);
	fu_struct_hpi_cfu_payload_cmd_set_flags(st_payload, FU_CFU_CONTENT_FLAG_FIRST_BLOCK);
	fu_struct_hpi_cfu_payload_cmd_set_length(st_payload, 52);
	fu_struct_hpi_cfu_payload_cmd_set_seq_number(st_payload, 1);
	ret = fu_struct_hpi_cfu_payload_cmd_set_data(st_payload, buf_payload->data + 5, 52, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_test_hpi_cfu_add_event_out(FU_HPI_CFU_DEVICE(device), 0x0220, st_payload->buf);
	fu_test_hpi_cfu_add_event_in(FU_HPI_CFU_DEVICE(device), 0, 0x22);

	ret = fu_device_write_firmware(device, firmware, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/hpi-cfu/device{short-block}", fu_hpi_cfu_device_short_block_func);
	return g_test_run();
}
