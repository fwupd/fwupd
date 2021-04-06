/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#include "fu-chunk.h"

#include "fu-analogix-common.h"
#include "fu-analogix-device.h"
#include "fu-analogix-firmware.h"

struct _FuAnalogixDevice {
	FuUsbDevice	 parent_instance;
	guint8		 iface_idx;		/* bInterfaceNumber */
	guint8		 ep_num;		/* bEndpointAddress */
	guint16		 chunk_len;		/* wMaxPacketSize */
	guint16		 custom_version;
	guint16		 fw_version;
};

G_DEFINE_TYPE (FuAnalogixDevice, fu_analogix_device, FU_TYPE_USB_DEVICE)

static void
fu_analogix_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	fu_common_string_append_kx (str, idt, "IfaceIdx", self->iface_idx);
	fu_common_string_append_kx (str, idt, "EpNum", self->ep_num);
	fu_common_string_append_kx (str, idt, "ChunkLen", self->chunk_len);
	fu_common_string_append_kx (str, idt, "CustomVersion", self->custom_version);
	fu_common_string_append_kx (str, idt, "FwVersion", self->fw_version);
}

static gboolean
fu_analogix_device_send (FuAnalogixDevice *self,
			 AnxBbRqtCode reqcode,
			 guint16 val0code,
			 guint16 index,
			 const guint8 *buf,
			 gsize bufsz,
			 GError **error)
{
	gsize actual_len = 0;
	g_autofree guint8 *buf_tmp = NULL;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz <= 64, FALSE);

	/* make mutable */
	buf_tmp = fu_memdup_safe (buf, bufsz, error);
	if (buf_tmp == NULL)
		return FALSE;

	/* send data to device */
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    reqcode, /* request */
					    val0code, /* value */
					    index, /* index */
					    buf_tmp, /* data */
					    bufsz, /* length */
					    &actual_len, /* actual length */
					    (guint) ANX_BB_TRANSACTION_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send data error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "send data length is incorrect");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_receive (FuAnalogixDevice *self,
			    AnxBbRqtCode reqcode,
			    guint16 val0code,
			    guint16 index,
			    guint8 *buf,
			    gsize bufsz,
			    GError **error)
{
	gsize actual_len = 0;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz <= 64, FALSE);

	/* get data from device */
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    reqcode, /* request */
					    val0code, /* value */
					    index,
					    buf, /* data */
					    bufsz,  /* length */
					    &actual_len, /* actual length */
					    (guint) ANX_BB_TRANSACTION_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "receive data error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "receive data length is incorrect");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_check_update_status (FuAnalogixDevice *self, GError **error)
{
	guint8 status = UPDATE_STATUS_INVALID;

	for (guint i = 0; i < 3000; i++) {
		if (!fu_analogix_device_receive (self,
						 ANX_BB_RQT_GET_UPDATE_STATUS,
						 0, 0,
						 &status, sizeof(status),
						 error))
			return FALSE;
		if (status == UPDATE_STATUS_ERROR) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "update status was error");
			return FALSE;
		}

		/* assumed success */
		if (status != UPDATE_STATUS_INVALID)
			return TRUE;

		/* wait 1ms */
		g_usleep (1000);
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "timed out: status was invalid");
	return FALSE;
}

static gboolean
fu_analogix_device_open (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS (fu_analogix_device_parent_class)->open (device, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, self->iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_setup (FuDevice *device, GError **error)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	guint8 buf_fw[2] = { 0x0 };
	guint8 buf_custom[2] = { 0x0 };
	g_autofree gchar *version = NULL;

	/* get OCM version */
	if (!fu_analogix_device_receive (self, ANX_BB_RQT_READ_FW_VER, 0, 0,
					 &buf_fw[1], 1, error))
		return FALSE;
	if (!fu_analogix_device_receive (self, ANX_BB_RQT_READ_FW_RVER, 0, 0,
				         &buf_fw[0], 1, error))
		return FALSE;

	/* TODO: get custom version */
	self->fw_version = fu_common_read_uint16 (buf_fw, G_BIG_ENDIAN);
	self->custom_version = fu_common_read_uint16 (buf_custom, G_BIG_ENDIAN);

	/* device version is both versions as a pair */
	version = g_strdup_printf ("%04x.%04x", self->custom_version, self->fw_version);
	fu_device_set_version (FU_DEVICE (device), version);
	return TRUE;
}

static gboolean
fu_analogix_device_find_interface (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = g_usb_device_get_interfaces (usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		if (g_usb_interface_get_class (intf) == BILLBOARD_CLASS &&
		    g_usb_interface_get_subclass (intf) == BILLBOARD_SUBCLASS &&
		    g_usb_interface_get_protocol (intf) == BILLBOARD_PROTOCOL) {
			GUsbEndpoint *ep;
			g_autoptr(GPtrArray) endpoints = NULL;

			endpoints = g_usb_interface_get_endpoints (intf);
			if (endpoints == NULL || endpoints->len == 0)
				continue;
			ep = g_ptr_array_index (endpoints, 0);
			self->iface_idx = g_usb_interface_get_number (intf);
			self->ep_num = g_usb_endpoint_get_address (ep) & 0x7f;
			self->chunk_len = g_usb_endpoint_get_maximum_packet_size (ep);
			return TRUE;
		}
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no update interface found");
	return FALSE;
}

static gboolean
fu_analogix_device_probe (FuDevice *device, GError **error)
{
	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS (fu_analogix_device_parent_class)->probe (device, error))
		return FALSE;
	if (!fu_analogix_device_find_interface (FU_USB_DEVICE (device), error)) {
		g_prefix_error (error, "failed to find update interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_analogix_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	guint16 main_ocm_ver = 0;
	guint16 custom_fw_version = 0;
	const AnxImgHeader *hdr = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) firmware = fu_analogix_firmware_new ();
	g_autoptr(GBytes) fw_hdr = NULL;

	/* get header */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	fw_hdr = fu_firmware_get_image_by_id_bytes (firmware,
						    FU_FIRMWARE_ID_HEADER,
						    error);
	if (fw_hdr == NULL)
		return NULL;

	/* TODO: move these as accessors as fu_analogix_firmware_get_foo() etc and hide AnxImgHeader from FuAnalogixDevice */
	hdr = (const AnxImgHeader *) g_bytes_get_data (fw_hdr, NULL);

	/* parse version: TODO: move to FuAnalogixFirmware->parse */
	main_ocm_ver = hdr->fw_ver;
	if (main_ocm_ver == 0)
		main_ocm_ver = self->fw_version;
	custom_fw_version = hdr->custom_ver;
	if (custom_fw_version == 0)
		custom_fw_version = self->custom_version;
	version = g_strdup_printf ("%04x.%04x", custom_fw_version, main_ocm_ver);
	fu_firmware_set_version (firmware, version);

	return g_steal_pointer (&firmware);
}

static gboolean
fu_analogix_device_program_flash (FuAnalogixDevice *self,
				  guint32 start_addr,
				  guint32 total_len,
				  guint32 len,
				  guint16 req_val,
				  guint32 base,
				  GBytes *source_buf,
				  GError **error)
{
	guint8 buf_init[4] = { 0x0 };
	g_autoptr(GBytes) block_bytes = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* offset into firmware */
	block_bytes = fu_common_bytes_new_offset (source_buf, base, len, error);
	if (block_bytes == NULL)
		return FALSE;

	/* initialization */
	fu_common_write_uint32 (buf_init, len, G_LITTLE_ENDIAN);
	if (!fu_analogix_device_send (self,
				      ANX_BB_RQT_SEND_UPDATE_DATA,
				      req_val,
				      0,
				      buf_init,
				      3,	/* TODO: check this is correct */
				      error)) {
		g_prefix_error (error, "program initialized failed: ");
		return FALSE;
	}
	if (!fu_analogix_device_check_update_status (self, error))
		return FALSE;

	/* write data */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (block_bytes, 0x00, 0x00,
						BILLBOARD_MAX_PACKET_SIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_analogix_device_send (self,
					      ANX_BB_RQT_SEND_UPDATE_DATA,
					      req_val,
					      i + 1,
					      fu_chunk_get_data (chk),
					      fu_chunk_get_data_sz (chk),
					      error)) {
			g_prefix_error (error, "failed send on chk %u: ", i);
			return FALSE;
		}
		if (!fu_analogix_device_check_update_status (self, error)) {
			g_prefix_error (error, "failed status on chk %u: ", i);
			return FALSE;
		}
		fu_device_set_progress_full (FU_DEVICE (self), i, chunks->len - 1);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	const AnxImgHeader *hdr = NULL;
	guint32 base = 0;
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* get header and payload */
	fw_hdr = fu_firmware_get_image_by_id_bytes (firmware,
						    FU_FIRMWARE_ID_HEADER,
						    error);
	if (fw_hdr == NULL)
		return FALSE;
	fw_payload = fu_firmware_get_image_by_id_bytes (firmware,
							FU_FIRMWARE_ID_PAYLOAD,
							error);
	if (fw_payload == NULL)
		return FALSE;

	/* set up the firmware header */
	hdr = (const AnxImgHeader *) g_bytes_get_data (fw_hdr, NULL);
	if (hdr == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "read image header error");
		return FALSE;
	}
	if (hdr->total_len > MAX_FILE_SIZE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "invalid payload length of firmware");
		return FALSE;
	}
	g_debug ("payload_len:0x%x,hdr->fw_start_addr:0x%x", hdr->total_len, hdr->fw_start_addr);

	/* OCM -> SECURE_TX -> SECURE_RX -> CUSTOM_DEF */
	if (hdr->custom_start_addr == FLASH_CUSTOM_ADDR &&
	    hdr->custom_payload_len > 0) {
		base = hdr->fw_payload_len + hdr->secure_tx_payload_len + hdr->secure_rx_payload_len;
		if (!fu_analogix_device_program_flash (self,
						       hdr->custom_start_addr,
						       hdr->total_len,
						       hdr->custom_payload_len,
						       ANX_BB_WVAL_UPDATE_CUSTOM_DEF,
						       base,
						       fw_payload,
						       error)) {
			g_prefix_error (error, "program custom define failed: ");
			return FALSE;
		}
	}
	if (hdr->secure_tx_start_addr == FLASH_TXFW_ADDR &&
	    hdr->secure_tx_payload_len > 0) {
		base = hdr->fw_payload_len;
		if (!fu_analogix_device_program_flash (self,
						       hdr->secure_tx_start_addr,
						       hdr->total_len,
						       hdr->secure_tx_payload_len,
						       ANX_BB_WVAL_UPDATE_SECURE_TX,
						       base,
						       fw_payload,
						       error)) {
			g_prefix_error (error, "program secure OCM TX failed: ");
			return FALSE;
		}
	}
	if (hdr->secure_rx_start_addr == FLASH_RXFW_ADDR &&
	    hdr->secure_rx_payload_len > 0) {
		base = hdr->fw_payload_len + hdr->secure_tx_payload_len;
		if (!fu_analogix_device_program_flash (self,
						       hdr->secure_rx_start_addr,
						       hdr->total_len,
						       hdr->secure_rx_payload_len,
						       ANX_BB_WVAL_UPDATE_SECURE_RX,
						       base,
						       fw_payload,
						       error)) {
			g_prefix_error (error, "program secure OCM RX failed: ");
			return FALSE;
		}
	}
	if (hdr->fw_start_addr == FLASH_OCM_ADDR && hdr->fw_payload_len > 0) {
		base = 0;
		if (!fu_analogix_device_program_flash (self,
						       hdr->fw_start_addr,
						       hdr->total_len,
						       hdr->fw_payload_len,
						       ANX_BB_WVAL_UPDATE_OCM,
						       base,
						       fw_payload,
						       error)) {
			g_prefix_error (error, "program OCM failed: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_close (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);

	/* release interface */
	if (!g_usb_device_release_interface (usb_device, self->iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS (fu_analogix_device_parent_class)->close (device, error);
}

static void
fu_analogix_device_init (FuAnalogixDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "com.analogix.bb");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_summary (FU_DEVICE (self), "Phoenix-Lite");
	fu_device_set_vendor (FU_DEVICE (self), "Analogix Semiconductor Inc.");
}

static void
fu_analogix_device_class_init (FuAnalogixDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_analogix_device_to_string;
	klass_device->write_firmware = fu_analogix_device_write_firmware;
	klass_device->setup = fu_analogix_device_setup;
	klass_device->open = fu_analogix_device_open;
	klass_device->probe = fu_analogix_device_probe;
	klass_device->prepare_firmware = fu_analogix_device_prepare_firmware;
	klass_device->close = fu_analogix_device_close;
}
