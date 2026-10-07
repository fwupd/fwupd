/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_ALC408X_FIRMWARE (fu_realtek_alc408x_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekAlc408xFirmware,
		     fu_realtek_alc408x_firmware,
		     FU,
		     REALTEK_ALC408X_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_realtek_alc408x_firmware_new(void);
guint16
fu_realtek_alc408x_firmware_get_chip(FuRealtekAlc408xFirmware *self);
