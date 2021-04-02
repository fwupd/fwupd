/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#include <string.h>

//#include "fu-chunk.h"

#include "fu-anx-udev-common.h"
#include "fu-anx-udev-device.h"
#include "fu-anx-udev-firmware.h"



struct _FuAnxUdevDevice {
	FuUsbDevice  parent_instance;
	guint8       iface_idx;         /* bInterfaceNumber */
	guint8       ep_num;                /* bEndpointAddress */
	guint16      chunk_len;         /* wMaxPacketSize */
	guint16      vid;
	guint16      pid;
	guint16      rev;
};

G_DEFINE_TYPE (FuAnxUdevDevice, fu_anx_udev_device, FU_TYPE_USB_DEVICE)
static gboolean
fu_anx_udev_device_send (FuAnxUdevDevice *self,
                       AnxBbRqtCode reqcode,
                       guint16 val0code,
               		   guint16 index,
                       guint8 *in,
                       gsize in_len,
                       GError **error)
{
    gsize actual_len = 0;
	/* check size */
	if (in_len > 64) {
		g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"input buffer too large");
		return FALSE;
	}
    g_return_val_if_fail (in != NULL, FALSE);
	/* send data to device */
	if (!g_usb_device_control_transfer (
			fu_usb_device_get_dev (FU_USB_DEVICE (self)),
			G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
			G_USB_DEVICE_REQUEST_TYPE_VENDOR,
			G_USB_DEVICE_RECIPIENT_DEVICE,
			reqcode, /* request */
			val0code, /* value */
			index, /* index */
			in, /* data */
			in_len, /* length */
			&actual_len, /* actual length */
			(guint)ANX_BB_TRANSACTION_TIMEOUT,
			NULL, error)) {
		g_prefix_error (error, "send data error: ");
		return FALSE;
	}
     if (actual_len != in_len)
     {
		g_prefix_error (error, "send data error count: ");
         return FALSE;
     }
	return TRUE;
}

static gboolean
fu_anx_udev_device_receive (FuAnxUdevDevice *self,
               				AnxBbRqtCode reqcode,
                       		guint16 val0code,
               				guint16 index,
                       		guint8 *out,
                       		gsize out_len,
                       		GError **error)
{
    gsize actual_len = 0;
	/* check size */
	if (out_len > 64) {
		g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"output buffer too large");
		return FALSE;
	}
    g_return_val_if_fail (out != NULL, FALSE);
    /* get data from device */
    if (!g_usb_device_control_transfer (
					fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					G_USB_DEVICE_RECIPIENT_DEVICE,
					reqcode, /* request */
					val0code, /* value */
					index, /* index */
					out, /* data */
					out_len,  /* length */
					&actual_len, /* actual length */
					(guint)ANX_BB_TRANSACTION_TIMEOUT,
					NULL, error)) {
			g_prefix_error (error, "receive data error: ");
			return FALSE;
    }
     if (actual_len != out_len)
     {
		g_prefix_error (error, "receive data error count: ");
         return FALSE;
     }
    return TRUE;
}

static gboolean
check_update_status(FuAnxUdevDevice *self)
{
	AnxUpdateStatus status = UPDATE_STATUS_INVALID;
	gint times = 30000;
	while ((status == UPDATE_STATUS_INVALID) && times > 0) {
		/*g_debug ("status:%d", (gint)status);*/
		if (!fu_anx_udev_device_receive (self, ANX_BB_RQT_GET_UPDATE_STATUS, 0, 
		0, (guint8 *)&status,1, NULL))
			return FALSE;
		if (status == UPDATE_STATUS_ERROR)
			return FALSE;
		times --;
	}
	if (times <= 0)
		return FALSE;
	return TRUE;
}

static gboolean
fu_anx_udev_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuAnxUdevDevice *self = FU_ANX_UDEV_DEVICE (device);

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
fu_anx_udev_device_setup (FuDevice *device, GError **error)
{
    FuAnxUdevDevice *self = FU_ANX_UDEV_DEVICE (device);
    guint8 fw_ver[2];
	guint8 cus_ver[2];
	guint16 fw_i_ver = 0;
	guint16 cus_i_ver = 0;
    g_autofree gchar *version = NULL;
    /*get OCM version*/
    if(!fu_anx_udev_device_receive (self, ANX_BB_RQT_READ_FW_VER, 0, 0,
									&fw_ver[1], 1, error))
        return FALSE;

    if(!fu_anx_udev_device_receive(self, ANX_BB_RQT_READ_FW_RVER, 0, 0,
												&fw_ver[0], 1, error))
        return FALSE;
	fw_i_ver = (fw_ver[1] << 8) | fw_ver[0];
	cus_i_ver = (cus_ver[1] << 8) | cus_ver[0];
    version = g_strdup_printf ("%04x.%04x", cus_i_ver, fw_i_ver);
    fu_device_set_version (FU_DEVICE (device), version);
	set_custom_version (cus_i_ver);
	set_fw_version (fw_i_ver);
    return TRUE;
}

static gboolean
fu_anx_udev_device_find_interface (FuUsbDevice *device,
                                      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuAnxUdevDevice *self = FU_ANX_UDEV_DEVICE (device);
	g_autoptr(GPtrArray) intfs = NULL;
	/* based on usb_updater2's find_interfacei() and find_endpoint() */

	intfs = g_usb_device_get_interfaces (usb_device, error);
	if (intfs == NULL)
	{
		g_debug ("anx_udev:fu_anx_udev_device_find_interface,intfs == NULL");
		return FALSE;
	}
	self->vid = g_usb_device_get_vid (usb_device);
	self->pid = g_usb_device_get_pid (usb_device);
	self->rev = g_usb_device_get_release (usb_device);
	g_debug ("USB: VID:%04X, PID:%04X, REV:%04X", self->vid, self->pid,
					self->rev);
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		if (g_usb_interface_get_class (intf) == BILLBOARD_CALSS &&
			g_usb_interface_get_subclass (intf) == BILLBOARD_SUBCLASS &&
			g_usb_interface_get_protocol (intf) == BILLBOARD_PROTOCOL) {
			//GUsbEndpoint *ep;
			g_autoptr(GPtrArray) endpoints = NULL;

			endpoints = g_usb_interface_get_endpoints (intf);
			if (NULL == endpoints)
					continue;
			/*if (endpoints->len == 0)
			{
					ep = endpoints;
			}
			else
			{
					ep = g_ptr_array_index (endpoints, 0);
			}
			g_debug ("anx_udev:fu_anx_udev_device_find_interface, NULL != endpoints");
			self->iface_idx = g_usb_interface_get_number (intf);
			self->ep_num = g_usb_endpoint_get_address (ep) & 0x7f;
			self->chunk_len = g_usb_endpoint_get_maximum_packet_size (ep);
            */
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
fu_anx_udev_device_probe (FuUsbDevice *device, GError **error)
{
	g_debug ("anx_udev:fu_anx_udev_device_probe");
	if (!fu_anx_udev_device_find_interface (device, error)) {
		g_prefix_error (error, "failed to find update interface: ");
		return FALSE;
	}
	set_custom_version (0);
	set_fw_version (0);
	/* set name and vendor */
	fu_device_set_summary (FU_DEVICE (device),
							"Analogix Phoenix-Lite");
	fu_device_set_vendor (FU_DEVICE (device), "Analogix Semiconductor Inc.");
	/* success */
	return TRUE;
}


static FuFirmware *
fu_anx_udev_device_prepare_firmware (FuDevice *device,
                                   GBytes *fw,
                                   FwupdInstallFlags flags,
                                   GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_anx_udev_firmware_new ();
	g_debug ("anx_udev:fu_anx_udev_device_prepare_firmware, flag = %d",
							(gint)flags);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean program_flash (guint32 start_addr, guint32 total_len,
								guint32 len, guint16 req_val,
								FuDevice *device, guint32 base,
								const guint8 *source_buf)
{
	static guint32 wrote_len = 0;
	guint32 packet_count;
	guint32 left_count;
	guint32 packet_index = 0;
	FuAnxUdevDevice *self = FU_ANX_UDEV_DEVICE (device);
	packet_count = len/BILLBOARD_MAX_PACKET_SIZE;
	left_count = len%BILLBOARD_MAX_PACKET_SIZE;
	if (source_buf == NULL)
			return FALSE;
	/*initialization*/
	if (!fu_anx_udev_device_send (self, ANX_BB_RQT_SEND_UPDATE_DATA, req_val, 0, 
							(guint8 *)&len, 3, NULL))
		return FALSE;

	if (!check_update_status (self))
		return FALSE;
	/*write data*/
	for (packet_index = 0; packet_index < packet_count; packet_index++)
	{
		fu_anx_udev_device_send (self, ANX_BB_RQT_SEND_UPDATE_DATA, req_val,
								packet_index+1, 
			(guint8 *)&source_buf[packet_index*BILLBOARD_MAX_PACKET_SIZE+base],
								BILLBOARD_MAX_PACKET_SIZE, NULL);
		if (!check_update_status (self))
		{
			g_debug ("Update failed with packet: %d, base:%x",
							(gint)packet_index, base);
			for (gint i = 0; i < BILLBOARD_MAX_PACKET_SIZE; i++) {
				g_debug ("index : %d, data :0x%x", i,
				source_buf[packet_index*BILLBOARD_MAX_PACKET_SIZE+i+base]);
			}
			return FALSE;
		}
		wrote_len += BILLBOARD_MAX_PACKET_SIZE;
		fu_device_set_progress_full (device, wrote_len, total_len);
	}
	if (left_count > 0) {
		fu_anx_udev_device_send (self, ANX_BB_RQT_SEND_UPDATE_DATA, req_val,
								packet_index+1, 
			(guint8 *)&source_buf[packet_index*BILLBOARD_MAX_PACKET_SIZE+base],
								left_count, NULL);
		if (!check_update_status (self))
				return FALSE;
		wrote_len += left_count;
		fu_device_set_progress_full (device, wrote_len, total_len);
	}
	return TRUE;
}

static gboolean
fu_anx_udev_device_write_firmware (FuDevice *device,
								FuFirmware *firmware,
								FwupdInstallFlags flags,
								GError **error)
{
	/*FuAnxUdevDevice *self = FU_ANX_UDEV_DEVICE (device);*/
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;
	const AnxImgHeader *buf = NULL;
	guint8 *fw_buf = NULL;
	guint32 payload_len;
	guint16 req_value = 0;
	guint32 base = 0;
	gboolean program_ret = FALSE;
	g_debug ("anx_udev:fu_anx_udev_device_write_firmware");
	/* get header and payload */
	fw_hdr = fu_firmware_get_image_by_id_bytes (firmware,
											FU_FIRMWARE_IMAGE_ID_HEADER,
											error);
	if (fw_hdr == NULL)
		return FALSE;
	fw_payload = fu_firmware_get_image_by_id_bytes (firmware,
												FU_FIRMWARE_IMAGE_ID_PAYLOAD,
												error);
	if (fw_payload == NULL)
		return FALSE;
	/*g_debug ("get img");*/
	/* set up the firmware header */
	buf = (const AnxImgHeader *)g_bytes_get_data (fw_hdr, NULL);
	if (buf == NULL) {
		g_set_error (error,
			G_IO_ERROR,
			G_IO_ERROR_FAILED,
			"read image header error");
		return FALSE;
	}
	payload_len = buf->total_len;
	if (payload_len > MAX_FILE_SIZE) {
		g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"invalid payload length of firmware");
		return FALSE;
	}
	g_debug ("payload_len:%d,buf->fw_start_addr:%d ", (gint)payload_len,
        	(gint)buf->fw_start_addr);
	fw_buf = (guint8 *)g_bytes_get_data (fw_payload, NULL);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if ((buf->custom_start_addr == FLASH_CUSTOM_ADDR) &&
					(buf->custom_payload_len > 0)) {
		req_value = ANX_BB_WVAL_UPDATE_CUSTOM_DEF;
		base = buf->fw_payload_len + buf->secure_tx_payload_len +
						buf->secure_rx_payload_len;
		program_ret = program_flash (buf->custom_start_addr, payload_len,
									buf->custom_payload_len,
									req_value, device, base,fw_buf);
		if (program_ret == FALSE)
		{
			g_set_error (error,
				G_IO_ERROR,
				G_IO_ERROR_INVALID_DATA,
				"Program Custom Define Failed");
			return FALSE;
		}
	}
	if ((buf->secure_tx_start_addr == FLASH_TXFW_ADDR) &&
					(buf->secure_tx_payload_len > 0)) {
		req_value = ANX_BB_WVAL_UPDATE_SECURE_TX;
		base = buf->fw_payload_len;
		program_ret = program_flash (buf->secure_tx_start_addr, payload_len,
									buf->secure_tx_payload_len,
									req_value, device, base,fw_buf);
		if (program_ret == FALSE)
		{
				g_set_error (error,
						G_IO_ERROR,
						G_IO_ERROR_INVALID_DATA,
						"Program Secure OCM TX Failed");
				return FALSE;
		}                                
	}
	if ((buf->secure_rx_start_addr == FLASH_RXFW_ADDR) &&
					(buf->secure_rx_payload_len > 0)) {
		req_value = ANX_BB_WVAL_UPDATE_SECURE_RX;
		base = buf->fw_payload_len + buf->secure_tx_payload_len;
		program_ret = program_flash (buf->secure_rx_start_addr,
									payload_len, buf->secure_rx_payload_len,
									req_value, device, base,fw_buf);
		if (program_ret == FALSE)
		{
				g_set_error (error,
						G_IO_ERROR,
						G_IO_ERROR_INVALID_DATA,
						"Program Secure OCM RX Failed");
				return FALSE;
		}
	}
	if ((buf->fw_start_addr == FLASH_OCM_ADDR) && (buf->fw_payload_len > 0)) {
		req_value = ANX_BB_WVAL_UPDATE_OCM;
		base = 0;
		program_ret = program_flash (buf->fw_start_addr, payload_len,
									buf->fw_payload_len,
									req_value, device, base, fw_buf);
		if (program_ret == FALSE)
		{
			g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"Program OCM Failed");
			return FALSE;
		}
	}
    return TRUE;
}

static gboolean
fu_anx_udev_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuAnxUdevDevice *self = FU_ANX_UDEV_DEVICE (device);
	/* release interface */
	if (!g_usb_device_release_interface (usb_device, self->iface_idx,
					G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}
	set_custom_version (0);
	set_fw_version (0);
	g_debug ("anx_udev:fu_anx_udev_device_close");
	/* success */
	return TRUE;
}


static void
fu_anx_udev_device_init (FuAnxUdevDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.analogix.bb");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_anx_udev_device_class_init (FuAnxUdevDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_anx_udev_device_write_firmware;
	klass_device->setup = fu_anx_udev_device_setup;
	klass_usb_device->open = fu_anx_udev_device_open;
	klass_usb_device->probe = fu_anx_udev_device_probe;
	klass_device->prepare_firmware = fu_anx_udev_device_prepare_firmware;
    klass_usb_device->close = fu_anx_udev_device_close;
}
