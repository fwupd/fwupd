/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#include <string.h>

#include "fu-chunk.h"

#include "fu-analogix-usbc-common.h"
#include "fu-analogix-usbc-device.h"
#include "fu-analogix-usbc-firmware.h"



struct _FuAnalogixUsbcDevice {
    FuUsbDevice  parent_instance;
    guint8       iface_idx;         /* bInterfaceNumber */
    guint8       ep_num;                /* bEndpointAddress */
    guint16      chunk_len;         /* wMaxPacketSize */
    guint16      vid;
    guint16      pid;
    guint16      rev;
    guint16      custom_version;
    guint16      fw_version;
};

G_DEFINE_TYPE (FuAnalogixUsbcDevice, fu_analogix_usbc_device, FU_TYPE_USB_DEVICE)
static gboolean
fu_analogix_usbc_device_send (FuAnalogixUsbcDevice *self,
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
        g_set_error (error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "send data error");
        return FALSE;
    }
     if (actual_len != in_len) {
        g_prefix_error (error, "receive data error count: ");
        g_set_error (error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "send data length is incorrect");
        return FALSE;
     }
    return TRUE;
}

static gboolean
fu_analogix_usbc_device_receive (FuAnalogixUsbcDevice *self,
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
check_update_status(FuAnalogixUsbcDevice *self)
{
    AnxUpdateStatus status = UPDATE_STATUS_INVALID;
    gint times = 30000;
    while ((status == UPDATE_STATUS_INVALID) && times > 0) {
        /* g_debug ("status:%d", (gint)status); */
        if (!fu_analogix_usbc_device_receive (self, ANX_BB_RQT_GET_UPDATE_STATUS, 0, 
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
fu_analogix_usbc_device_open (FuDevice *device, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
    FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device);
    /* FuUsbDevice->open */
    if (!FU_DEVICE_CLASS (fu_analogix_usbc_device_parent_class)->open (device, error))
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
fu_analogix_usbc_device_setup (FuDevice *device, GError **error)
{
    FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device);
    guint8 fw_ver[2];
    guint8 cus_ver[2] = {0};
    guint16 fw_i_ver = 0;
    guint16 cus_i_ver = 0;
    g_autofree gchar *version = NULL;
    /* get OCM version */
    if(!fu_analogix_usbc_device_receive (self, ANX_BB_RQT_READ_FW_VER, 0, 0,
                                    &fw_ver[1], 1, error))
        return FALSE;

    if(!fu_analogix_usbc_device_receive(self, ANX_BB_RQT_READ_FW_RVER, 0, 0,
                                        &fw_ver[0], 1, error))
        return FALSE;
    /* TODO:get Custom Version */
    fw_i_ver = (fw_ver[1] << 8) | fw_ver[0];
    cus_i_ver = (cus_ver[1] << 8) | cus_ver[0];
    version = g_strdup_printf ("%04x.%04x", cus_i_ver, fw_i_ver);
    fu_device_set_version (FU_DEVICE (device), version);
    self->custom_version = cus_i_ver;
    self->fw_version = fw_i_ver;
    return TRUE;
}

static gboolean
fu_analogix_usbc_device_find_interface (FuUsbDevice *device,
                                      GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev (device);
    FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device);
    g_autoptr(GPtrArray) intfs = NULL;
    /* based on usb_updater2's find_interfacei() and find_endpoint() */

    intfs = g_usb_device_get_interfaces (usb_device, error);
    if (intfs == NULL)
    {
        g_set_error_literal (error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_NOT_FOUND,
                        "no interface found");        
        return FALSE;
    }
    self->vid = g_usb_device_get_vid (usb_device);
    self->pid = g_usb_device_get_pid (usb_device);
    self->rev = g_usb_device_get_release (usb_device);
    g_debug ("USB: VID:%04X, PID:%04X, REV:%04X", self->vid, self->pid,
                    self->rev);
    for (guint i = 0; i < intfs->len; i++) {
        GUsbInterface *intf = g_ptr_array_index (intfs, i);
        if (g_usb_interface_get_class (intf) == BILLBOARD_CLASS &&
            g_usb_interface_get_subclass (intf) == BILLBOARD_SUBCLASS &&
            g_usb_interface_get_protocol (intf) == BILLBOARD_PROTOCOL) {
            //GUsbEndpoint *ep;
            g_autoptr(GPtrArray) endpoints = NULL;

            endpoints = g_usb_interface_get_endpoints (intf);
            if (NULL == endpoints)
                    continue;
            /* if (endpoints->len == 0)
            {
                    ep = endpoints;
            }
            else
            {
                    ep = g_ptr_array_index (endpoints, 0);
            }
            g_debug ("analogix_usbc:fu_analogix_usbc_device_find_interface, NULL != endpoints");
            self->iface_idx = g_usb_interface_get_number (intf);
            self->ep_num = g_usb_endpoint_get_address (ep) & 0x7f;
            self->chunk_len = g_usb_endpoint_get_maximum_packet_size (ep); */
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
fu_analogix_usbc_device_probe (FuDevice *device, GError **error)
{
    g_debug ("analogix_usbc:fu_analogix_usbc_device_probe");
    /* FuUsbDevice->probe */
    if (!FU_DEVICE_CLASS (fu_analogix_usbc_device_parent_class)->probe (device, error))
        return FALSE;
    if (!fu_analogix_usbc_device_find_interface (FU_USB_DEVICE (device), error)) {
        g_prefix_error (error, "failed to find update interface: ");
        return FALSE;
    }
    /* set name and vendor */
    fu_device_set_summary (FU_DEVICE (device), "Phoenix-Lite");
    fu_device_set_vendor (FU_DEVICE (device), "Analogix Semiconductor Inc.");
    /* success */
    return TRUE;
}


static FuFirmware *
fu_analogix_usbc_device_prepare_firmware (FuDevice *device,
                                   GBytes *fw,
                                   FwupdInstallFlags flags,
                                   GError **error)
{
    guint16 main_ocm_ver = 0;
    guint16 custom_fw_version = 0;
    g_autofree gchar *version = NULL;
    const AnxImgHeader *buf = NULL;
    g_autoptr(GBytes) fw_hdr = NULL;
    FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device);
    g_autoptr(FuFirmware) firmware = fu_analogix_usbc_firmware_new ();
    g_debug ("analogix_usbc:fu_analogix_usbc_device_prepare_firmware, flag = %d",
                            (gint)flags);
    if (!fu_firmware_parse (firmware, fw, flags, error)) {
        g_set_error (error,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "parse firmware file error");
         return NULL;   
    }

    /* get header */
    fw_hdr = fu_firmware_get_image_by_id_bytes (firmware,
                                            FU_FIRMWARE_ID_HEADER,
                                            error);
    if (fw_hdr == NULL) {
        g_set_error (error,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "parse firmware file error");
        return FALSE;
    }
    buf = (const AnxImgHeader *)g_bytes_get_data (fw_hdr, NULL);
    if (buf == NULL) {
        g_set_error (error,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "read image header error");
        return FALSE;
    }
    /* parse version */
    main_ocm_ver = buf->fw_ver;
    if (main_ocm_ver == 0)
        main_ocm_ver = self->fw_version;
    custom_fw_version = buf->custom_ver;
    if (custom_fw_version == 0)
        custom_fw_version = self->custom_version;
    version = g_strdup_printf ("%04x.%04x", custom_fw_version, main_ocm_ver);
    fu_firmware_set_version (firmware, version);
    return g_steal_pointer (&firmware);
}

static gboolean program_flash (guint32 start_addr, guint32 total_len,
                                guint32 len, guint16 req_val,
                                FuDevice *device, guint32 base,
                                GBytes *source_buf, GError **error)
{
    static guint32 wrote_len = 0;
    guint32 packet_index = 0;
    g_autoptr(GBytes) block_bytes = NULL;
    g_autoptr(GPtrArray) chunks = NULL;
    FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device);
    if (source_buf == NULL)
        return FALSE;
    block_bytes =  fu_common_bytes_new_offset (source_buf,
                                            base,
                                            len,
                                            error);
    if (block_bytes == NULL)
        return FALSE;
    chunks = fu_chunk_array_new_from_bytes (block_bytes,
                                            0x00,
                                            0x00,
                                            BILLBOARD_MAX_PACKET_SIZE);
    /* initialization */
    if (!fu_analogix_usbc_device_send (self, ANX_BB_RQT_SEND_UPDATE_DATA, req_val, 0, 
                            (guint8 *)&len, 3, NULL)) {
        g_set_error (error,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "Program initialized failed");
        return FALSE;
    }
    if (!check_update_status (self)) {
        g_set_error (error,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "Program initialized failed");
        return FALSE;
    }
    /* write data */
    for (packet_index = 0; packet_index < chunks->len; packet_index++)
    {
        FuChunk *chk = g_ptr_array_index (chunks, packet_index);
        fu_analogix_usbc_device_send (self, ANX_BB_RQT_SEND_UPDATE_DATA, req_val,
                                packet_index+1, 
                                (guint8 *)fu_chunk_get_data (chk),
                                fu_chunk_get_data_sz (chk), NULL);
        if (!check_update_status (self))
        {
            g_debug ("Update failed with packet: %d, base:%x",
                            (gint)packet_index, base);
            if (!check_update_status (self)) {
                g_set_error (error,
                    G_IO_ERROR,
                    G_IO_ERROR_FAILED,
                    "Program initialized failed");
                return FALSE;
            }
        }
        wrote_len += fu_chunk_get_data_sz (chk);
        fu_device_set_progress_full (device, wrote_len, total_len);
    }
    return TRUE;
}

static gboolean
fu_analogix_usbc_device_write_firmware (FuDevice *device,
                                FuFirmware *firmware,
                                FwupdInstallFlags flags,
                                GError **error)
{
    /* FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device); */
    g_autoptr(GBytes) fw_hdr = NULL;
    g_autoptr(GBytes) fw_payload = NULL;
    const AnxImgHeader *buf = NULL;
    guint32 payload_len;
    guint16 req_value = 0;
    guint32 base = 0;
    gboolean program_ret = FALSE;
    g_autoptr(GError) error_local = NULL;
    g_debug ("analogix_usbc:fu_analogix_usbc_device_write_firmware");
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
    /* g_debug ("get img"); */
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
    fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
    if ((buf->custom_start_addr == FLASH_CUSTOM_ADDR) &&
                    (buf->custom_payload_len > 0)) {
        req_value = ANX_BB_WVAL_UPDATE_CUSTOM_DEF;
        base = buf->fw_payload_len + buf->secure_tx_payload_len +
                        buf->secure_rx_payload_len;
        program_ret = program_flash (buf->custom_start_addr, payload_len,
                                    buf->custom_payload_len,
                                    req_value, device, base,fw_payload,
                                    &error_local);
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
                                    req_value, device, base,fw_payload,
                                    &error_local);
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
                                    req_value, device, base,fw_payload,
                                    &error_local);
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
                                    req_value, device, base, fw_payload,
                                    &error_local);
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
fu_analogix_usbc_device_close (FuDevice *device, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
    FuAnalogixUsbcDevice *self = FU_ANALOGIX_USBC_DEVICE (device);
    /* release interface */
    if (!g_usb_device_release_interface (usb_device, self->iface_idx,
                    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                    error)) {
        g_prefix_error (error, "failed to release interface: ");
        return FALSE;
    }
    g_debug ("analogix_usbc:fu_analogix_usbc_device_close");
    /* FuUsbDevice->close */
    return FU_DEVICE_CLASS (fu_analogix_usbc_device_parent_class)->close (device, error);
}


static void
fu_analogix_usbc_device_init (FuAnalogixUsbcDevice *self)
{
    fu_device_add_protocol (FU_DEVICE (self), "com.analogix.bb");
    fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
    fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_analogix_usbc_device_class_init (FuAnalogixUsbcDeviceClass *klass)
{
    FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
    klass_device->write_firmware = fu_analogix_usbc_device_write_firmware;
    klass_device->setup = fu_analogix_usbc_device_setup;
    klass_device->open = fu_analogix_usbc_device_open;
    klass_device->probe = fu_analogix_usbc_device_probe;
    klass_device->prepare_firmware = fu_analogix_usbc_device_prepare_firmware;
    klass_device->close = fu_analogix_usbc_device_close;
}
