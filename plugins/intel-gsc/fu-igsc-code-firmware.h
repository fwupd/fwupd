/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IGSC_CODE_FIRMWARE (fu_igsc_code_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIgscCodeFirmware,
		     fu_igsc_code_firmware,
		     FU,
		     IGSC_CODE_FIRMWARE,
		     FuIfwiFptFirmware)

FuFirmware *
fu_igsc_code_firmware_new(void);
guint32
fu_igsc_code_firmware_get_hw_sku(FuIgscCodeFirmware *self);
