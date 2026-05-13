/*
 * Copyright 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_PIXART_RF_DEVICE_MODEL_NAME_LEN 12 /* bytes */

#define FU_TYPE_PIXART_RF_FIRMWARE (fu_pixart_rf_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuPixartRfFirmware, fu_pixart_rf_firmware, FU, PIXART_RF_FIRMWARE, FuFirmware)

FuFirmware *
fu_pixart_rf_firmware_new(void);
const gchar *
fu_pixart_rf_firmware_get_model_name(FuPixartRfFirmware *self);

gboolean
fu_pixart_rf_firmware_is_hpac(FuPixartRfFirmware *self);
