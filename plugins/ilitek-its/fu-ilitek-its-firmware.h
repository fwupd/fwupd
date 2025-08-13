/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ILITEK_ITS_FIRMWARE (fu_ilitek_its_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIlitekItsFirmware,
		     fu_ilitek_its_firmware,
		     FU,
		     ILITEK_ITS_FIRMWARE,
		     FuIhexFirmware)

FuFirmware *
fu_ilitek_its_firmware_new(void);

const gchar *
fu_ilitek_its_firmware_get_ic_name(FuIlitekItsFirmware *self) G_GNUC_NON_NULL(1);
