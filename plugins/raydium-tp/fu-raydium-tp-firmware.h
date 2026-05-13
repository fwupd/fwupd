/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RAYDIUM_TP_FIRMWARE (fu_raydium_tp_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuRaydiumTpFirmware,
		     fu_raydium_tp_firmware,
		     FU,
		     RAYDIUM_TP_FIRMWARE,
		     FuFirmware)

guint16
fu_raydium_tp_firmware_get_vendor_id(FuRaydiumTpFirmware *self);
guint16
fu_raydium_tp_firmware_get_product_id(FuRaydiumTpFirmware *self);
