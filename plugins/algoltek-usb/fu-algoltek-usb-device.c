/*
 * Copyright (C) 2023 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-algoltek-usb-common.h"
#include "fu-algoltek-usb-device.h"
#include "fu-algoltek-usb-firmware.h"
// #include "fu-algoltek-usb-struct.h"

struct _FuAlgoltekUsbDevice {
    FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbDevice, fu_algoltek_usb_device, FU_TYPE_USB_DEVICE)

static GByteArray *
algoltek_device_rdr(FuAlgoltekUsbDevice *self, int address, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();

    /* first byte is length */
    fu_byte_array_set_size(buf, 11, 0x0);
    buf->data[0] = 5;
    buf->data[1] = ALGOLTEK_RDR;
    fu_memwrite_uint16(buf->data + 2, address, G_BIG_ENDIAN);
    buf->data[10] = ~fu_sum8(buf->data, buf->len) + 1;
    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_RDR,
                                       address, /* value */
                                       0xFFFF,  /* index */
                                       buf->data,
                                       buf->len,
                                       NULL,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return NULL;
    }

    /* success */
    return g_steal_pointer(&buf);
}

static GByteArray *
algoltek_device_rdv(FuAlgoltekUsbDevice *self, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();
    g_autoptr(GByteArray) versionData = g_byte_array_new();
    gsize actual_length = 0;

    /* first byte is length */
    fu_byte_array_set_size(buf, 64, 0x0);
    buf->data[0] = 3;
    buf->data[1] = ALGOLTEK_RDV;
    buf->data[63] = ~fu_sum8(buf->data, buf->len) + 1;
    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_RDV,
                                       0xFFFF, /* value */
                                       0xFFFF, /* index */
                                       buf->data,
                                       buf->len,
                                       &actual_length,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return NULL;
    }

    if (actual_length != buf->len) {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "got 0x%x but requested 0x%x",
                    (guint)actual_length,
                    (guint)buf->len);
        return NULL;
    }

    /* Remove cmd and length */
    for (guint32 i = 2; i < buf->len; i++) {
        if (buf->data[i] < 128)
            fu_byte_array_append_uint8(versionData, buf->data[i]);
    }
    /* success */
    return g_steal_pointer(&versionData);
}

static guint16
fu_algoltek_readout_value(GByteArray *back_data)
{
    guint16 back_data_addr;
    guint16 back_data_value;

    back_data_addr = fu_memread_uint16(back_data->data + 2, G_BIG_ENDIAN);
    if (back_data_addr == AG_UPDATE_STATUS)
        back_data_value = back_data->data[0];
    else
        back_data_value = fu_memread_uint16(back_data->data + 4, G_BIG_ENDIAN);
    return back_data_value;
}

static gboolean
algoltek_device_en(FuAlgoltekUsbDevice *self, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();

    fu_byte_array_set_size(buf, 11, 0x0);
    buf->data[0] = 3;
    buf->data[1] = ALGOLTEK_EN;
    buf->data[10] = ~fu_sum8(buf->data, buf->len) + 1;
    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_EN,
                                       0, /* value */
                                       0, /* index */
                                       buf->data,
                                       buf->data[0],
                                       NULL,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return FALSE;
    }

    return TRUE;
}

static gboolean
algoltek_device_rst(FuAlgoltekUsbDevice *self, guint8 number, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();
    fu_byte_array_set_size(buf, 11, 0x0);
    buf->data[0] = 4;
    buf->data[1] = ALGOLTEK_RST;
    buf->data[2] = number;
    buf->data[10] = ~fu_sum8(buf->data, buf->len) + 1;
    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_RST,
                                       0, /* value */
                                       0, /* index */
                                       buf->data,
                                       buf->data[0],
                                       NULL,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return FALSE;
    }
    return TRUE;
}

static gboolean
algoltek_device_wrr(FuAlgoltekUsbDevice *self, int address, int input_value, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();
    fu_byte_array_set_size(buf, 11, 0x0);
    buf->data[0] = 7;
    buf->data[1] = ALGOLTEK_WRR;
    fu_memwrite_uint16(buf->data + 2, address, G_BIG_ENDIAN);
    fu_memwrite_uint16(buf->data + 4, input_value, G_BIG_ENDIAN);
    buf->data[10] = ~fu_sum8(buf->data, buf->len) + 1;
    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_WRR,
                                       0, /* value */
                                       0, /* index */
                                       buf->data,
                                       buf->data[0],
                                       NULL,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return FALSE;
    }

    return TRUE;
}

static gboolean
algoltek_device_ISP(FuAlgoltekUsbDevice *self, GByteArray *ISP_data, int address, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) part_ISP_data = g_byte_array_new();
    guint rest_transfer_data_size = ISP_data->len;
    guint data_addr = 0;
    guint start_index = 0;
    guint transfer_data_size = 0;
    guint basic_data_size = 5;
    guint max_packet_size = 64;

    while (rest_transfer_data_size > 0) {
        data_addr = address + start_index;
        if (rest_transfer_data_size > (max_packet_size - basic_data_size)) {
            transfer_data_size = max_packet_size - basic_data_size;
        } else {
            transfer_data_size = rest_transfer_data_size;
        }
        fu_byte_array_set_size(part_ISP_data, transfer_data_size + basic_data_size, 0);
        part_ISP_data->data[0] = (guint8)(transfer_data_size + basic_data_size);
        part_ISP_data->data[1] = ALGOLTEK_ISP;
        fu_memwrite_uint16(part_ISP_data->data + 2, data_addr, G_BIG_ENDIAN);

        for (guint i = 0; i < transfer_data_size; i++) {
            part_ISP_data->data[4 + i] = ISP_data->data[i + start_index];
        }

        part_ISP_data->data[part_ISP_data->len - 1] =
            ~fu_sum8(part_ISP_data->data, part_ISP_data->len) + 1;

        if (!g_usb_device_control_transfer(usb_device,
                                           G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                           G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                           G_USB_DEVICE_RECIPIENT_INTERFACE,
                                           ALGOLTEK_ISP,
                                           0, /* value */
                                           0, /* index */
                                           part_ISP_data->data,
                                           part_ISP_data->data[0],
                                           NULL,
                                           ALGOLTEK_DEVICE_USB_TIMEOUT,
                                           NULL,
                                           error)) {
            g_prefix_error(error, "Device communication failure: ");
            return FALSE;
        }

        start_index += transfer_data_size;
        rest_transfer_data_size -= transfer_data_size;
    }
    return TRUE;
}

static gboolean
algoltek_device_bot(FuAlgoltekUsbDevice *self, int address, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();

    fu_byte_array_set_size(buf, 11, 0x0);
    buf->data[0] = 5;
    buf->data[1] = ALGOLTEK_BOT;
    fu_memwrite_uint16(buf->data + 2, address, G_BIG_ENDIAN);
    buf->data[10] = ~fu_sum8(buf->data, buf->len) + 1;
    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_BOT,
                                       0, /* value */
                                       0, /* index */
                                       buf->data,
                                       buf->data[0],
                                       NULL,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return FALSE;
    }

    return TRUE;
}

static gboolean
algoltek_device_ers(FuAlgoltekUsbDevice *self, GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) buf = g_byte_array_new();

    fu_byte_array_set_size(buf, 2, 0x0);

    if (!g_usb_device_control_transfer(usb_device,
                                       G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                       G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                       G_USB_DEVICE_RECIPIENT_INTERFACE,
                                       ALGOLTEK_ERS,
                                       0, /* value */
                                       0, /* index */
                                       buf->data,
                                       buf->len,
                                       NULL,
                                       ALGOLTEK_DEVICE_USB_TIMEOUT,
                                       NULL,
                                       error)) {
        g_prefix_error(error, "Device communication failure: ");
        return FALSE;
    }

    return TRUE;
}

static gboolean
fu_algoltek_device_status_check(FuAlgoltekUsbDevice *self, GError **error)
{
    g_autoptr(GByteArray) update_status_array = g_byte_array_new();
    guint retryTimes = 0;
    guint16 update_status;
Retry:
    update_status_array = algoltek_device_rdr(self, AG_UPDATE_STATUS, error);
    if (update_status_array == NULL)
        return FALSE;
    update_status = fu_algoltek_readout_value(update_status_array);
    switch (update_status) {
    case AG_UPDATE_PASS:
        break;
    case AG_UPDATE_FAIL:
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Update procedure is failed.");
        return FALSE;
    default:
        retryTimes++;
        if (retryTimes < 10)
            goto Retry;
        else
            return FALSE;
    }
    return TRUE;
}

static gboolean
algoltek_device_wrf(FuAlgoltekUsbDevice *self,
                    GByteArray *firmware_data,
                    FuProgress *progress,
                    GError **error)
{
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
    g_autoptr(GByteArray) transfer_parameter = g_byte_array_new();
    g_autoptr(GByteArray) part_firmware = g_byte_array_new();
    guint rest_transfer_data_size = firmware_data->len;
    guint data_addr = 0;
    guint start_index = 0;
    guint max_packet_size = 64;
    guint transfer_data_size = 0;
    guint count_check = 0;
    guint16 value;
    guint16 index;

    while (rest_transfer_data_size > 0) {
        count_check++;
        data_addr = start_index;
        if (rest_transfer_data_size > max_packet_size)
            transfer_data_size = max_packet_size;
        else
            transfer_data_size = rest_transfer_data_size;

        fu_byte_array_set_size(transfer_parameter, 4, 0);

        if (count_check == 256 / max_packet_size)
            transfer_parameter->data[0] = 1;
        else
            transfer_parameter->data[0] = 0;

        fu_memwrite_uint24(transfer_parameter->data + 1, data_addr, G_BIG_ENDIAN);

        value = fu_memread_uint16(transfer_parameter->data, G_BIG_ENDIAN);
        index = fu_memread_uint16(transfer_parameter->data + 2, G_BIG_ENDIAN);

        fu_byte_array_set_size(part_firmware, transfer_data_size, 0);

        for (guint i = 0; i < transfer_data_size; i++)
            part_firmware->data[i] = firmware_data->data[i + start_index];

        if (!g_usb_device_control_transfer(usb_device,
                                           G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                           G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                           G_USB_DEVICE_RECIPIENT_INTERFACE,
                                           ALGOLTEK_WRF,
                                           value, /* value */
                                           index, /* index */
                                           part_firmware->data,
                                           part_firmware->len,
                                           NULL,
                                           ALGOLTEK_DEVICE_USB_TIMEOUT,
                                           NULL,
                                           error)) {
            g_prefix_error(error, "Device communication failure: ");
            return FALSE;
        }

        start_index += transfer_data_size;
        rest_transfer_data_size -= transfer_data_size;
        if (count_check == (256 / max_packet_size) || rest_transfer_data_size == 0) {
            count_check = 0;
            if (!fu_algoltek_device_status_check(self, error))
                return FALSE;
        }
        fu_progress_set_percentage_full(progress, start_index, firmware_data->len);
    }
    return TRUE;
}

static gboolean
fu_algoltek_usb_device_setup(FuDevice *device, GError **error)
{
    FuAlgoltekUsbDevice *self = FU_ALGOLTEK_USB_DEVICE(device);
    GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
    g_autoptr(GByteArray) version_data = NULL;
    g_autofree gchar *version_str = NULL;

    /* UsbDevice->setup */
    if (!FU_DEVICE_CLASS(fu_algoltek_usb_device_parent_class)->setup(device, error))
        return FALSE;

    version_data = algoltek_device_rdv(self, error);
    version_str = g_strdup_printf("%s", version_data->data);

    g_assert(self != NULL);
    g_assert(usb_device != NULL);
    fu_device_set_version(device, version_str);

    /* success */
    return TRUE;
}

static gboolean
fu_algoltek_usb_device_write_firmware(FuDevice *device,
                                      FuFirmware *firmware,
                                      FuProgress *progress,
                                      FwupdInstallFlags flags,
                                      GError **error)
{
    FuAlgoltekUsbDevice *self = FU_ALGOLTEK_USB_DEVICE(device);
    g_autoptr(GBytes) blob_ISP = NULL;
    g_autoptr(GBytes) blob_payload = NULL;
    g_autoptr(GByteArray) fw_ISP = g_byte_array_new();
    g_autoptr(GByteArray) fw_payload = g_byte_array_new();

    /* progress */
    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, NULL);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);

    if (!algoltek_device_en(self, error)) {
        g_prefix_error(error, "System activation failure: ");
        return FALSE;
    }

    if (!algoltek_device_rst(self, 2, error)) {
        g_prefix_error(error, "System reboot failure: ");
        return FALSE;
    }

    fu_device_sleep(FU_DEVICE(self), 900);

    if (!algoltek_device_wrr(self, 0x80AD, 0, error)) {
        g_prefix_error(error, "Data write failure: ");
        return FALSE;
    }

    algoltek_device_wrr(self, 0x80C0, 0, error);

    algoltek_device_wrr(self, 0x80C9, 0, error);

    algoltek_device_wrr(self, 0x80D1, 0, error);

    algoltek_device_wrr(self, 0x80D9, 0, error);

    algoltek_device_wrr(self, 0x80E1, 0, error);

    algoltek_device_wrr(self, 0x80E9, 0, error);

    if (!algoltek_device_rst(self, 0, error)) {
        g_prefix_error(error, "System reboot failure: ");
        return FALSE;
    }

    fu_device_sleep(FU_DEVICE(self), 500);

    /* get ISP image */
    blob_ISP = fu_firmware_get_image_by_id_bytes(firmware, "ISP", error);
    if (blob_ISP == NULL)
        return FALSE;
    fu_byte_array_append_bytes(fw_ISP, blob_ISP);
    if (!algoltek_device_ISP(self, fw_ISP, AG_ISP_ADDR, error)) {
        g_prefix_error(error, "ISP failure: ");
        return FALSE;
    }

    if (!algoltek_device_bot(self, AG_ISP_ADDR, error)) {
        g_prefix_error(error, "System boot failure: ");
        return FALSE;
    }

    fu_device_sleep(FU_DEVICE(self), 1000);

    if (!algoltek_device_ers(self, error)) {
        g_prefix_error(error, "Data clear failure: ");
        return FALSE;
    }

    fu_device_sleep(FU_DEVICE(self), 500);

    /* get payload image */
    blob_payload = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
    if (blob_payload == NULL)
        return FALSE;
    fu_byte_array_append_bytes(fw_payload, blob_payload);
    if (!algoltek_device_wrf(self, fw_payload, progress, error)) {
        g_prefix_error(error, "Data write failure: ");
        return FALSE;
    }
    fu_progress_step_done(progress);

    if (!algoltek_device_rst(self, 1, error)) {
        g_prefix_error(error, "System reboot failure: ");
        return FALSE;
    }
    /* success! */
    return TRUE;
}

static void
fu_algoltek_usb_device_set_progress(FuDevice *self, FuProgress *progress)
{
    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_algoltek_usb_device_init(FuAlgoltekUsbDevice *self)
{
    fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
    fu_device_add_protocol(FU_DEVICE(self), "tw.com.algoltek.usb");
    fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
    fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
    fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ALGOLTEK_USB_FIRMWARE);
}

static void
fu_algoltek_usb_device_class_init(FuAlgoltekUsbDeviceClass *klass)
{
    FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
    klass_device->setup = fu_algoltek_usb_device_setup;
    klass_device->write_firmware = fu_algoltek_usb_device_write_firmware;
    klass_device->set_progress = fu_algoltek_usb_device_set_progress;
}
