/*
 * Copyright 2024 Chris Hofstaedtler <ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_RP_PICO_DEVICE (fu_rp_pico_device_get_type())
G_DECLARE_FINAL_TYPE(FuRpPicoDevice, fu_rp_pico_device, FU, RP_PICO_DEVICE, FuUsbDevice)
