/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-elanfp-file-control.h"
#include "fu-elanfp-usb-device.h"

#define ELAN_EP_CMD_OUT	     (0x01 | 0x00)
#define ELAN_EP_CMD_IN	     (0x02 | 0x80)
#define ELAN_EP_MOC_CMD_IN   (0x04 | 0x80)
#define ELAN_EP_IMG_IN	     (0x03 | 0x80)
#define ELANFP_USB_INTERFACE 0

#define CTRL_SEND_TIMEOUT_MS		 3000
#define BULK_SEND_TIMEOUT_MS		 1000
#define BULK_RECV_TIMEOUT_MS		 1000
#define ELANFP_FLASH_TRANSFER_BLOCK_SIZE 1024

#define PROTOCOL_VERSION_2 0x02
#define PROTOCOL_VERSION_4 0x04

#define FIRMWARE_UPDATE_OFFER_SKIP   0x00
#define FIRMWARE_UPDATE_OFFER_ACCEPT 0x01
#define FIRMWARE_UPDATE_OFFER_REJECT 0x02

#define FIRMWARE_UPDATE_FLAG_FIRST_BLOCK 0x80
#define FIRMWARE_UPDATE_FLAG_LAST_BLOCK	 0x40

#define STA_REJECT_OLD_FIRMWARE	      0x00
#define STA_REJECT_SWAP_PENDING	      0x02
#define STA_REJECT_WRONG_BANK	      0x04
#define STA_REJECT_SIGN_RULE	      0xE0
#define STA_REJECT_VER_RELEASE_DEBUG  0xE1
#define STA_REJECT_DEBUG_SAME_VERSION 0xE2

#define FIRMWARE_UPDATE_SUCCESS		   0x00
#define FIRMWARE_UPDATE_ERROR_WRITE	   0x02
#define FIRMWARE_UPDATE_ERROR_VERIFY	   0x04
#define FIRMWARE_UPDATE_ERROR_SIGNATURE	   0x06
#define FIRMWARE_UPDATE_ERROR_INVALID_ADDR 0x09
#define FIRMWARE_UPDATE_ERROR_NO_OFFER	   0x0A
#define FIRMWARE_UPDATE_ERROR_INVALID	   0x0B

#define REPORT_ID_FW_VERSION_FEATURE 0x20
#define REPORT_ID_OFFER_COMMAND	     0x25
#define REPORT_ID_OFFER_RESPONSE     0x25
#define REPORT_ID_PAYLOAD_COMMAND    0x20
#define REPORT_ID_PAYLOAD_RESPONSE   0x22

#define REQTYPE_GET_VERSION 0xC1
#define REQTYPE_COMMAND	    0x41

struct _FuElanfpUsbDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuElanfpUsbDevice, fu_elanfp_usb_device, FU_TYPE_USB_DEVICE)

gboolean
iap_send_command(GUsbDevice *usb_device,
		 guint8 reqType,
		 guint8 request,
		 guint8 *pbuff,
		 gsize len,
		 GError **error)
{
	gsize actual = 0;

	if (pbuff == NULL) {
		g_prefix_error(error, "send command - buffer is null: ");
		return FALSE;
	}

	if (len == 0) {
		g_prefix_error(error, "send command - buffer length is zero: ");
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   request, // request
					   0x00,    // value
					   0x00,    // index
					   pbuff,
					   len,
					   &actual,
					   CTRL_SEND_TIMEOUT_MS,
					   NULL,
					   error)) {
		g_prefix_error(error, "send command - failed to send command: ");
		return FALSE;
	}

	if (actual != len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "send length (%d) is not match with the request (%d)",
			    (gint32)actual,
			    (gint32)len);

		return FALSE;
	}

	return TRUE;
}

gboolean
iap_recv_status(GUsbDevice *usb_device, guint8 *pbuff, gsize len, GError **error)
{
	gsize actual = 0;

	if (pbuff == NULL) {
		g_prefix_error(error, "received status - buffer is null: ");
		return FALSE;
	}

	if (len == 0) {
		g_prefix_error(error, "received status - buffer length is zero: ");
		return FALSE;
	}

	if (!g_usb_device_bulk_transfer(usb_device,
					ELAN_EP_CMD_IN,
					pbuff,
					len,
					&actual,
					BULK_RECV_TIMEOUT_MS,
					NULL,
					error)) {
		g_prefix_error(error, "received status - failed to received status: ");
		return FALSE;
	}

	if (actual != len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "received length (%d) is not match with the request (%d)",
			    (gint32)actual,
			    (gint32)len);

		return FALSE;
	}

	return TRUE;
}

gboolean
run_iap_process(FuElanfpUsbDevice *self, FuFirmware *firmware, GError **error)
{
	S2F_FILE s2f_file;
	PAYLOAD_HEADER *ppayload_header = NULL;
	guint8 databuf[61] = {0};
	guint8 recvbuf[17] = {0};
	guint32 payload_header_length = 5;
	guint16 pkg_index = 0;
	guint32 payload_offset = 0;
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));

	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_offer_a = NULL;
	g_autoptr(GBytes) fw_offer_b = NULL;
	g_autoptr(GBytes) fw_payload_a = NULL;
	g_autoptr(GBytes) fw_payload_b = NULL;

	g_autoptr(GPtrArray) chunks = NULL;

	g_return_val_if_fail(firmware != NULL, FALSE);

	if (!fU_elanfp_file_ctrl_binary_verify(firmware, error)) {
		g_prefix_error(error, "run iap process - binary verify fail: ");
		return FALSE;
	}

	fw_offer_a = fu_firmware_get_image_by_id_bytes(firmware, FW_SET_ID_OFFER_A, error);
	fw_offer_b = fu_firmware_get_image_by_id_bytes(firmware, FW_SET_ID_OFFER_B, error);
	fw_payload_a = fu_firmware_get_image_by_id_bytes(firmware, FW_SET_ID_PAYLOAD_A, error);
	fw_payload_b = fu_firmware_get_image_by_id_bytes(firmware, FW_SET_ID_PAYLOAD_B, error);

	s2f_file.pOffer[0] = g_bytes_get_data(fw_offer_a, &s2f_file.OfferLength[0]);
	s2f_file.pOffer[1] = g_bytes_get_data(fw_offer_b, &s2f_file.OfferLength[1]);
	s2f_file.pPayload[0] = g_bytes_get_data(fw_payload_a, &s2f_file.PayloadLength[0]);
	s2f_file.pPayload[1] = g_bytes_get_data(fw_payload_b, &s2f_file.PayloadLength[1]);

	s2f_file.Tag[0][0] = 0x41;
	s2f_file.Tag[0][1] = 0x00;
	s2f_file.Tag[1][0] = 0x42;
	s2f_file.Tag[1][1] = 0x00;

	/* Send Offer */
	for (int i = 0; i < 2; i++) {
		memset(databuf, 0x00, sizeof(databuf));
		memset(recvbuf, 0x00, sizeof(recvbuf));
		databuf[0] = REPORT_ID_OFFER_COMMAND;

		if (!fu_memcpy_safe(databuf,
				    sizeof(databuf),
				    1,
				    s2f_file.pOffer[i],
				    s2f_file.OfferLength[i],
				    0,
				    s2f_file.OfferLength[i],
				    error)) {
			g_prefix_error(error,
				       "run iap process - memory copy for offer %s fail: ",
				       s2f_file.Tag[i]);
			return FALSE;
		}

		g_debug("send offer start");

		if (!iap_send_command(usb_device,
				      REQTYPE_COMMAND,
				      REPORT_ID_OFFER_COMMAND,
				      databuf,
				      s2f_file.OfferLength[i] + 1,
				      error)) {
			g_prefix_error(error, "run iap process - send offer command fail: ");
			return FALSE;
		}

		if (!iap_recv_status(usb_device, recvbuf, s2f_file.OfferLength[i] + 1, error)) {
			g_prefix_error(error, "run iap process - received offer status fail: ");
			return FALSE;
		}

		if (recvbuf[13] == FIRMWARE_UPDATE_OFFER_ACCEPT) {
			/* Payload */
			g_debug("run iap - Offer %s Accepted", s2f_file.Tag[i]);

			pkg_index = 1;
			payload_offset = 0;
			while (payload_offset < s2f_file.PayloadLength[i]) {
				memset(databuf, 0x00, sizeof(databuf));
				memset(recvbuf, 0x00, sizeof(recvbuf));

				ppayload_header =
				    (PAYLOAD_HEADER *)(s2f_file.pPayload[i] + payload_offset);

				databuf[0] = REPORT_ID_PAYLOAD_COMMAND;
				databuf[1] =
				    (pkg_index == 1)
					? FIRMWARE_UPDATE_FLAG_FIRST_BLOCK
					: ((payload_offset + payload_header_length +
					    ppayload_header->Length) >= s2f_file.PayloadLength[i]
					       ? FIRMWARE_UPDATE_FLAG_LAST_BLOCK
					       : 0x00);
				databuf[2] = ppayload_header->Length;

				if (!fu_common_write_uint16_safe(databuf,
								 sizeof(databuf),
								 3,
								 pkg_index,
								 G_LITTLE_ENDIAN,
								 error)) {
					g_prefix_error(error,
						       "run iap process - assign pkg index info to "
						       "buffer for offer %s fail: ",
						       s2f_file.Tag[i]);
					return FALSE;
				}

				if (!fu_common_write_uint32_safe(databuf,
								 sizeof(databuf),
								 5,
								 ppayload_header->Address,
								 G_LITTLE_ENDIAN,
								 error)) {
					g_prefix_error(error,
						       "run iap process - assign address info to "
						       "buffer for offer %s fail: ",
						       s2f_file.Tag[i]);
					return FALSE;
				}

				if (!fu_memcpy_safe(databuf,
						    sizeof(databuf),
						    9,
						    s2f_file.pPayload[i],
						    s2f_file.PayloadLength[i],
						    payload_offset + payload_header_length,
						    databuf[2],
						    error)) {
					g_prefix_error(
					    error,
					    "run iap process - memory copy for offer %s fail: ",
					    s2f_file.Tag[i]);
					return FALSE;
				}

				if (!iap_send_command(usb_device,
						      REQTYPE_COMMAND,
						      REPORT_ID_PAYLOAD_COMMAND,
						      databuf,
						      sizeof(databuf),
						      error)) {
					g_prefix_error(
					    error,
					    "run iap process - send payload command fail: ");
					return FALSE;
				}

				if (!iap_recv_status(usb_device, recvbuf, sizeof(recvbuf), error)) {
					g_prefix_error(
					    error,
					    "run iap process - received payload status fail: ");
					return FALSE;
				}

				if (recvbuf[5] == FIRMWARE_UPDATE_SUCCESS) {
					if (databuf[1] == FIRMWARE_UPDATE_FLAG_LAST_BLOCK) {
						fu_device_set_progress_full(
						    FU_DEVICE(self),
						    (gsize)payload_offset +
							(payload_header_length + databuf[2]),
						    (gsize)s2f_file.PayloadLength[i]);

						g_debug("run iap - iap bank-%s update completely, "
							"wait device "
							"reset !",
							s2f_file.Tag[i]);

					} else {
						fu_device_set_progress_full(
						    FU_DEVICE(self),
						    (gsize)payload_offset,
						    (gsize)s2f_file.PayloadLength[i]);
					}
				} else {
					if (recvbuf[5] == FIRMWARE_UPDATE_ERROR_WRITE)
						g_debug("run iap - payload %s : write fail, "
							"sequence no : "
							"0x%08X",
							s2f_file.Tag[i],
							*(guint32 *)(recvbuf + 1));
					else if (recvbuf[5] == FIRMWARE_UPDATE_ERROR_VERIFY)
						g_debug("run iap - payload %s : verify fail, "
							"sequence no : "
							"0x%08X",
							s2f_file.Tag[i],
							*(guint32 *)(recvbuf + 1));
					else if (recvbuf[5] == FIRMWARE_UPDATE_ERROR_SIGNATURE)
						g_debug("run iap - payload %s : signature error, "
							"sequence no : "
							"0x%08X",
							s2f_file.Tag[i],
							*(guint32 *)(recvbuf + 1));
					else if (recvbuf[5] == FIRMWARE_UPDATE_ERROR_INVALID_ADDR)
						g_debug("run iap - payload %s : invalid address, "
							"sequence no : "
							"0x%08X",
							s2f_file.Tag[i],
							*(guint32 *)(recvbuf + 1));
					else if (recvbuf[5] == FIRMWARE_UPDATE_ERROR_NO_OFFER)
						g_debug("run iap - payload %s : no offer error, "
							"sequence "
							"no : 0x%08X",
							s2f_file.Tag[i],
							*(guint32 *)(recvbuf + 1));
					else if (recvbuf[5] == FIRMWARE_UPDATE_ERROR_INVALID)
						g_debug("run iap - payload %s : invalid error, "
							"sequence no "
							": 0x%08X",
							s2f_file.Tag[i],
							*(guint32 *)(recvbuf + 1));
					else
						g_debug("run iap - payload %s status : 0x%02X, "
							"sequence no "
							": 0x%08X",
							s2f_file.Tag[i],
							recvbuf[5],
							*(guint32 *)(recvbuf + 1));

					return FALSE;
				}

				payload_offset += (payload_header_length + databuf[2]);

				pkg_index++;
			}
		} else if (recvbuf[13] == FIRMWARE_UPDATE_OFFER_REJECT) {
			if (recvbuf[9] == STA_REJECT_OLD_FIRMWARE)
				g_debug("run iap - offer-%s reject : OLD_FIRMWARE",
					s2f_file.Tag[i]);
			else if (recvbuf[9] == STA_REJECT_SWAP_PENDING)
				g_debug("run iap - offer-%s reject : SWAP_PENDING",
					s2f_file.Tag[i]);
			else if (recvbuf[9] == STA_REJECT_WRONG_BANK)
				g_debug("run iap - offer-%s reject : WRONG_BANK", s2f_file.Tag[i]);
			else if (recvbuf[9] == STA_REJECT_SIGN_RULE)
				g_debug("run iap - offer-%s reject : SIGN_RULE", s2f_file.Tag[i]);
			else if (recvbuf[9] == STA_REJECT_VER_RELEASE_DEBUG)
				g_debug("run iap - offer-%s reject : VER_RELEASE_DEBUG",
					s2f_file.Tag[i]);
			else if (recvbuf[9] == STA_REJECT_DEBUG_SAME_VERSION)
				g_debug("run iap - offer-%s reject : DEBUG_SAME_VERSION",
					s2f_file.Tag[i]);
			else
				g_debug("run iap - Offer-%s reject : 0x%02X",
					s2f_file.Tag[i],
					recvbuf[9]);
		} else if (recvbuf[13] == FIRMWARE_UPDATE_OFFER_SKIP)
			g_debug("run iap - offer-%s skip", s2f_file.Tag[i]);
		else
			g_debug("run iap - offer-%s status : 0x%02X", s2f_file.Tag[i], recvbuf[13]);
	}

	return TRUE;
}

static gboolean
fu_elanfp_usb_device_open(FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS(fu_elanfp_usb_device_parent_class)->open(device, error))
		return FALSE;

	if (!g_usb_device_claim_interface(usb_device,
					  ELANFP_USB_INTERFACE,
					  G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					  error)) {
		g_prefix_error(error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_usb_device_close(FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	if (!g_usb_device_release_interface(usb_device,
					    ELANFP_USB_INTERFACE,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error)) {
		g_prefix_error(error, "failed to release interface: ");
		return FALSE;
	}

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS(fu_elanfp_usb_device_parent_class)->close(device, error);
}

static gboolean
fu_elanfp_usb_device_do_xfer(FuElanfpUsbDevice *self,
			     guint8 *outbuf,
			     gsize outlen,
			     guint8 *inbuf,
			     gsize inlen,
			     gboolean allow_less,
			     gsize *rxed_count,
			     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual = 0;

	/* send data out */
	if (outbuf != NULL && outlen > 0) {
		if (!g_usb_device_bulk_transfer(usb_device,
						ELAN_EP_CMD_OUT,
						outbuf,
						outlen,
						&actual,
						BULK_SEND_TIMEOUT_MS,
						NULL,
						error)) {
			return FALSE;
		}
		if (actual != outlen) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_PARTIAL_INPUT,
				    "only sent %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT " bytes",
				    actual,
				    outlen);
			return FALSE;
		}
	}

	/* read reply back */
	if (inbuf != NULL && inlen > 0) {
		actual = 0;
		if (!g_usb_device_bulk_transfer(usb_device,
						ELAN_EP_IMG_IN,
						inbuf,
						inlen,
						&actual,
						BULK_RECV_TIMEOUT_MS,
						NULL,
						error)) {
			return FALSE;
		}
		if (actual != inlen && !allow_less) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_PARTIAL_INPUT,
				    "only received %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT " bytes",
				    actual,
				    outlen);
			return FALSE;
		}
	}

	if (rxed_count != NULL)
		*rxed_count = actual;

	return TRUE;
}

static gboolean
fu_elanfp_usb_device_setup(FuDevice *device, GError **error)
{
	FuElanfpUsbDevice *self = FU_ELANFP_USB_DEVICE(device);
	const gchar *fw_ver_str = NULL;
	guint16 fw_ver;
	guint8 usb_buf[2] = {0x40, 0x19};
	gsize actual_len = 0;

	if (!fu_elanfp_usb_device_do_xfer(self,
					  (guint8 *)&usb_buf,
					  sizeof(usb_buf),
					  usb_buf,
					  sizeof(usb_buf),
					  TRUE,
					  &actual_len,
					  error)) {
		g_prefix_error(error, "failed to device setup: ");
		return FALSE;
	}

	fw_ver = fu_common_read_uint16(usb_buf, G_BIG_ENDIAN);

	fw_ver_str = g_strdup_printf("%04x", fw_ver);

	g_debug("fw version %04x", fw_ver);

	fu_device_set_version(device, fw_ver_str);

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_usb_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuElanfpUsbDevice *self = FU_ELANFP_USB_DEVICE(device);

	if (!run_iap_process(self, firmware, error)) {
		g_prefix_error(error, "device write firmware - iap fail: ");
		return FALSE;
	}

	g_debug("device write firmware - iap success !!");

	return TRUE;
}

static gboolean
fu_elanfp_usb_device_attach(FuDevice *device, GError **error)
{
	FuElanfpUsbDevice *self = FU_ELANFP_USB_DEVICE(device);
	fu_device_set_status(FU_DEVICE(self), FWUPD_STATUS_DEVICE_RESTART);

	/* success */
	return TRUE;
}

static void
fu_elanfp_usb_device_init(FuElanfpUsbDevice *device)
{
	FuElanfpUsbDevice *self = FU_ELANFP_USB_DEVICE(device);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 5000);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elanfp");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-On-Chip Fingerprint Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Elanfp");
	fu_device_set_install_duration(FU_DEVICE(self), 10);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x90000);
}

static void
fu_elanfp_usb_device_class_init(FuElanfpUsbDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->attach = fu_elanfp_usb_device_attach;
	klass_device->setup = fu_elanfp_usb_device_setup;
	klass_device->write_firmware = fu_elanfp_usb_device_write_firmware;
	klass_device->open = fu_elanfp_usb_device_open;
	klass_device->close = fu_elanfp_usb_device_close;
}
