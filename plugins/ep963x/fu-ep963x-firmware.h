/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_EP963X_FIRMWARE (fu_ep963x_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuEp963xFirmware, fu_ep963x_firmware, FU, EP963X_FIRMWARE, FuFirmware)

FuFirmware		*fu_ep963x_firmware_new		(void);
