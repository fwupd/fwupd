/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-elanfp-device.h"
#include "fu-elanfp-firmware.h"

#define ELAN_EP_CMD_OUT	     (0x01 | 0x00)
#define ELAN_EP_CMD_IN	     (0x02 | 0x80)
#define ELAN_EP_MOC_CMD_IN   (0x04 | 0x80)
#define ELAN_EP_IMG_IN	     (0x03 | 0x80)
#define ELANFP_USB_INTERFACE 0

#define CTRL_SEND_TIMEOUT_MS 3000
#define BULK_SEND_TIMEOUT_MS 1000
#define BULK_RECV_TIMEOUT_MS 3000

#define REPORT_ID_FW_VERSION_FEATURE 0x20
#define REPORT_ID_OFFER_COMMAND	     0x25
#define REPORT_ID_OFFER_RESPONSE     0x25
#define REPORT_ID_PAYLOAD_COMMAND    0x20
#define REPORT_ID_PAYLOAD_RESPONSE   0x22

#define REQTYPE_GET_VERSION 0xC1
#define REQTYPE_COMMAND	    0x41

struct _FuElanfpDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuElanfpDevice, fu_elanfp_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_elanfp_iap_send_command(FuElanfpDevice *self,
			   guint8 request_type,
			   guint8 request,
			   const guint8 *buf,
			   gsize bufsz,
			   GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual = 0;
	guint8 buftmp[61] = {request, 0};

	if (buf != NULL) {
		if (!fu_memcpy_safe(buftmp,
				    sizeof(buftmp),
				    0x1, /* dst */
				    buf,
				    bufsz,
				    0x0, /* src */
				    bufsz,
				    error))
			return FALSE;
	}
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   request, /* request */
					   0x00,    /* value */
					   0x00,    /* index */
					   buftmp,
					   bufsz + 1,
					   &actual,
					   CTRL_SEND_TIMEOUT_MS,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to send command: ");
		return FALSE;
	}
	if (actual != bufsz + 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "send length (%u) is not match with the request (%u)",
			    (guint)actual,
			    (guint)bufsz + 1);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_elanfp_iap_recv_status(FuElanfpDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual = 0;

	if (!g_usb_device_bulk_transfer(usb_device,
					ELAN_EP_CMD_IN,
					buf,
					bufsz,
					&actual,
					BULK_RECV_TIMEOUT_MS,
					NULL,
					error)) {
		g_prefix_error(error, "failed to receive status: ");
		return FALSE;
	}
	if (actual != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "received length (%u) is not match with the request (%u)",
			    (guint)actual,
			    (guint)bufsz);

		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_elanfp_device_do_xfer(FuElanfpDevice *self,
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
fu_elanfp_device_setup(FuDevice *device, GError **error)
{
	FuElanfpDevice *self = FU_ELANFP_DEVICE(device);
	guint16 fw_ver;
	guint8 usb_buf[2] = {0x40, 0x19};
	g_autofree gchar *fw_ver_str = NULL;

	/* get version */
	if (!fu_elanfp_device_do_xfer(self,
				      (guint8 *)&usb_buf,
				      sizeof(usb_buf),
				      usb_buf,
				      sizeof(usb_buf),
				      TRUE,
				      NULL,
				      error)) {
		g_prefix_error(error, "failed to device setup: ");
		return FALSE;
	}
	fw_ver = fu_memread_uint16(usb_buf, G_BIG_ENDIAN);
	fw_ver_str = g_strdup_printf("%04x", fw_ver);
	fu_device_set_version(device, fw_ver_str);

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_device_write_payload(FuElanfpDevice *self,
			       FuFirmware *payload,
			       FuProgress *progress,
			       GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	/* write each chunk */
	chunks = fu_firmware_get_chunks(payload, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 databuf[60] = {0};
		guint8 recvbuf[17] = {0};

		/* flags */
		if (i == 0)
			databuf[0] = FU_CFU_DEVICE_FLAG_FIRST_BLOCK;
		else if (i == chunks->len - 1)
			databuf[0] = FU_CFU_DEVICE_FLAG_LAST_BLOCK;

		/* length */
		databuf[1] = fu_chunk_get_data_sz(chk);

		/* sequence number */
		if (!fu_memwrite_uint16_safe(databuf,
					     sizeof(databuf),
					     0x2,
					     i + 1,
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		/* address */
		if (!fu_memwrite_uint32_safe(databuf,
					     sizeof(databuf),
					     0x4,
					     fu_chunk_get_address(chk),
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		/* data */
		if (!fu_memcpy_safe(databuf,
				    sizeof(databuf),
				    0x8, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error, "memory copy for payload fail: ");
			return FALSE;
		}
		if (!fu_elanfp_iap_send_command(self,
						REQTYPE_COMMAND,
						REPORT_ID_PAYLOAD_COMMAND,
						databuf,
						sizeof(databuf),
						error)) {
			g_prefix_error(error, "send payload command fail: ");
			return FALSE;
		}
		if (!fu_elanfp_iap_recv_status(self, recvbuf, sizeof(recvbuf), error)) {
			g_prefix_error(error, "received payload status fail: ");
			return FALSE;
		}
		if (recvbuf[5] != FU_CFU_DEVICE_STATUS_SUCCESS) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to send chunk %u: %s",
				    i + 1,
				    fu_cfu_device_status_to_string(recvbuf[5]));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elanfp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuElanfpDevice *self = FU_ELANFP_DEVICE(device);
	guint i;
	struct {
		const gchar *tag;
		guint8 offer_idx;
		guint8 payload_idx;
	} items[] = {
	    {"A", FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_A, FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_A},
	    {"B", FU_ELANTP_FIRMWARE_IDX_CFU_OFFER_B, FU_ELANTP_FIRMWARE_IDX_CFU_PAYLOAD_B},
	    {NULL, FU_ELANTP_FIRMWARE_IDX_END, FU_ELANTP_FIRMWARE_IDX_END}};
	g_autoptr(FuFirmware) payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "payload");

	/* send offers */
	for (i = 0; items[i].tag != NULL; i++) {
		g_autoptr(GBytes) offer = NULL;
		guint8 recvbuf[17] = {0};

		offer = fu_firmware_get_image_by_idx_bytes(firmware, items[i].offer_idx, error);
		if (offer == NULL)
			return FALSE;
		if (!fu_elanfp_iap_send_command(self,
						REQTYPE_COMMAND,
						REPORT_ID_OFFER_COMMAND,
						g_bytes_get_data(offer, NULL),
						g_bytes_get_size(offer),
						error)) {
			g_prefix_error(error, "send offer command fail: ");
			return FALSE;
		}
		if (!fu_elanfp_iap_recv_status(self, recvbuf, sizeof(recvbuf), error)) {
			g_prefix_error(error, "received offer status fail: ");
			return FALSE;
		}
		g_debug("offer-%s status:%s reject:%s",
			items[i].tag,
			fu_cfu_device_offer_to_string(recvbuf[13]),
			fu_cfu_device_reject_to_string(recvbuf[9]));
		if (recvbuf[13] == FU_CFU_DEVICE_OFFER_ACCEPT)
			break;
	}
	if (items[i].tag == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no CFU offer was accepted");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send payload */
	payload = fu_firmware_get_image_by_idx(firmware, items[i].payload_idx, error);
	if (payload == NULL)
		return FALSE;
	if (!fu_elanfp_device_write_payload(self, payload, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_elanfp_device_init(FuElanfpDevice *device)
{
	FuElanfpDevice *self = FU_ELANFP_DEVICE(device);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 5000);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elanfp");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-On-Chip Fingerprint Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Elan");
	fu_device_set_install_duration(FU_DEVICE(self), 10);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max(FU_DEVICE(self), 0x90000);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELANFP_FIRMWARE);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), ELANFP_USB_INTERFACE);
}

static void
fu_elanfp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_elanfp_device_class_init(FuElanfpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_elanfp_device_setup;
	klass_device->write_firmware = fu_elanfp_device_write_firmware;
	klass_device->set_progress = fu_elanfp_device_set_progress;
}
