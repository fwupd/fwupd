/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RAYDIUM_TP_IMAGE (fu_raydium_tp_image_get_type())

G_DECLARE_FINAL_TYPE(FuRaydiumTpImage, fu_raydium_tp_image, FU, RAYDIUM_TP_IMAGE, FuFirmware)

guint32
fu_raydium_tp_image_get_checksum(FuRaydiumTpImage *self);
