/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-flashrom-device.h"

#define FU_TYPE_FLASHROM_LSPCON_I2C_SPI_DEVICE \
	(fu_flashrom_lspcon_i2c_spi_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFlashromLspconI2cSpiDevice,
		      fu_flashrom_lspcon_i2c_spi_device, FU,
		      FLASHROM_LSPCON_I2C_SPI_DEVICE, FuFlashromDevice)
