/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_GL32XX_DEVICE (fu_genesys_gl32xx_device_get_type())

G_DECLARE_FINAL_TYPE(FuGenesysGl32xxDevice,
		     fu_genesys_gl32xx_device,
		     FU,
		     GENESYS_GL32XX_DEVICE,
		     FuUdevDevice)
