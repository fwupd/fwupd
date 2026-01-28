/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RAYDIUM_TP_FIRMWARE (fu_raydium_tp_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuRaydiumtpFirmware,
		     fu_raydium_tp_firmware,
		     FU,
		     RAYDIUM_TP_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_raydium_tp_firmware_new(void);

guint16
fu_raydium_tp_firmware_get_vendor_id(FuRaydiumtpFirmware *self);

guint16
fu_raydium_tp_firmware_get_product_id(FuRaydiumtpFirmware *self);

guint32
fu_raydium_tp_firmware_get_fw_base(FuRaydiumtpFirmware *self);

guint32
fu_raydium_tp_firmware_get_desc_base(FuRaydiumtpFirmware *self);

guint32
fu_raydium_tp_firmware_get_fw_start(FuRaydiumtpFirmware *self);

guint32
fu_raydium_tp_firmware_get_fw_len(FuRaydiumtpFirmware *self);

guint32
fu_raydium_tp_firmware_get_desc_start(FuRaydiumtpFirmware *self);

guint32
fu_raydium_tp_firmware_get_desc_len(FuRaydiumtpFirmware *self);
