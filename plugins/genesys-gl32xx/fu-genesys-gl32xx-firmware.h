/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_GL32XX_FIRMWARE (fu_genesys_gl32xx_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysGl32xxFirmware,
		     fu_genesys_gl32xx_firmware,
		     FU,
		     GENESYS_GL32XX_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_genesys_gl32xx_firmware_new(void);
