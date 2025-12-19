/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RAYDIUMTP_FIRMWARE (fu_raydiumtp_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuRaydiumtpFirmware, fu_raydiumtp_firmware, FU, RAYDIUMTP_FIRMWARE, FuFirmware)

FuFirmware *
fu_raydiumtp_firmware_new(void);

gboolean 
fu_raydiumtp_firmware_parse(FuRaydiumtpFirmware *self, GInputStream *stream, guint16 pid, GError **error);

guint16 
fu_raydiumtp_firmware_get_vendor_id(FuRaydiumtpFirmware *self);

guint16 
fu_raydiumtp_firmware_get_product_id(FuRaydiumtpFirmware *self);

guint32 
fu_raydiumtp_firmware_get_fw_base(FuRaydiumtpFirmware *self);

guint32 
fu_raydiumtp_firmware_get_desc_base(FuRaydiumtpFirmware *self);

guint32 
fu_raydiumtp_firmware_get_fw_start(FuRaydiumtpFirmware *self);

guint32 
fu_raydiumtp_firmware_get_fw_len(FuRaydiumtpFirmware *self);

guint32 
fu_raydiumtp_firmware_get_desc_start(FuRaydiumtpFirmware *self);

guint32 
fu_raydiumtp_firmware_get_desc_len(FuRaydiumtpFirmware *self);

