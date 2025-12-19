/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydiumtp-common.h"
#include "fu-raydiumtp-firmware.h"
#include "fu-raydiumtp-struct.h"

struct _FuRaydiumtpFirmware
{
    FuFirmware parent_instance;
    guint16 vendor_id;
    guint16 product_id;
    guint32 fw_base;
    guint32 desc_base;
    guint32 fw_start;
    guint32	fw_len;
    guint32 desc_start;
    guint32	desc_len;
};

G_DEFINE_TYPE(FuRaydiumtpFirmware, fu_raydiumtp_firmware, FU_TYPE_FIRMWARE)

guint16 fu_raydiumtp_firmware_get_vendor_id(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->vendor_id;
}

guint16 fu_raydiumtp_firmware_get_product_id(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->product_id;
}

guint32 fu_raydiumtp_firmware_get_fw_base(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->fw_base;
}

guint32 fu_raydiumtp_firmware_get_desc_base(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->desc_base;
}

guint32 fu_raydiumtp_firmware_get_fw_start(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->fw_start;
}

guint32 fu_raydiumtp_firmware_get_fw_len(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->fw_len;
}

guint32 fu_raydiumtp_firmware_get_desc_start(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->desc_start;
}

guint32 fu_raydiumtp_firmware_get_desc_len(FuRaydiumtpFirmware *self)
{
    g_return_val_if_fail(FU_IS_RAYDIUMTP_FIRMWARE(self), 0);
    return self->desc_len;
}

gboolean
fu_raydiumtp_firmware_parse(FuRaydiumtpFirmware *self,
                            GInputStream *stream,
                            guint16 device_id,
                            GError **error)
{
    g_autoptr(FuStructRaydiumtpFwHdr) st = NULL;
    g_autoptr(GBytes) fw_bytes = NULL;
    gsize fw_size = 0;
    gsize streamsz = 0;

    if (!fu_input_stream_size(stream, &streamsz, error))
        return FALSE;

    if (streamsz < FU_STRUCT_RAYDIUMTP_FW_HDR_SIZE)
    {
        g_set_error(error,
                            FWUPD_ERROR,
                            FWUPD_ERROR_INVALID_FILE,
                            "stream was too small");
        return FALSE;
    }

    st = fu_struct_raydiumtp_fw_hdr_parse_stream(stream, 0x0, error);
    if (st == NULL)
        return FALSE;

    if (fu_struct_raydiumtp_fw_hdr_get_vendor_id(st) != VENDOR_ID)
    {
        g_set_error(error,
                    FWUPD_ERROR,
                    FWUPD_ERROR_INVALID_FILE,
                    "Unexpected vendor ID (expected 0x2386)");
        return FALSE;
    }

    if (fu_struct_raydiumtp_fw_hdr_get_product_id(st) != device_id)
    {
        g_set_error(error,
                    FWUPD_ERROR,
                    FWUPD_ERROR_INVALID_FILE,
                    "Product ID mismatch (expected %u)", device_id);
        return FALSE;
    }

    fw_bytes = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
    if (fw_bytes == NULL)
        return FALSE;

    fw_size = g_bytes_get_size(fw_bytes);

    if (fw_size < st->buf->len)
    {
        g_set_error(error,
                    FWUPD_ERROR,
                    FWUPD_ERROR_INVALID_FILE,
                    "Firmware file too small");
        return FALSE;
    }

	fu_firmware_set_bytes(FU_FIRMWARE(self), fw_bytes);

    self->vendor_id = fu_struct_raydiumtp_fw_hdr_get_vendor_id(st);
    self->product_id = fu_struct_raydiumtp_fw_hdr_get_product_id(st);
    self->fw_base = fu_struct_raydiumtp_fw_hdr_get_fw_base(st);
    self->desc_base = fu_struct_raydiumtp_fw_hdr_get_desc_base(st);
    self->fw_start = fu_struct_raydiumtp_fw_hdr_get_start(st);
    self->fw_len = fu_struct_raydiumtp_fw_hdr_get_length(st);
    self->desc_start = fu_struct_raydiumtp_fw_hdr_get_desc_start(st);
    self->desc_len = fu_struct_raydiumtp_fw_hdr_get_desc_length(st);

    /* success */
    return TRUE;
}

static void
fu_raydiumtp_firmware_init(FuRaydiumtpFirmware *self)
{
    /* no-op */
}

static void
fu_raydiumtp_firmware_class_init(FuRaydiumtpFirmwareClass *klass)
{
    /* no-op */
}

FuFirmware *
fu_raydiumtp_firmware_new(void)
{
    return FU_FIRMWARE(g_object_new(FU_TYPE_RAYDIUMTP_FIRMWARE, NULL));
}
