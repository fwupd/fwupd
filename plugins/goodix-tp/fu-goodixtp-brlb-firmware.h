/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-goodixtp-firmware.h"

#define FU_TYPE_GOODIXTP_BRLB_FIRMWARE (fu_goodixtp_brlb_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuGoodixtpBrlbFirmware,
		     fu_goodixtp_brlb_firmware,
		     FU,
		     GOODIXTP_BRLB_FIRMWARE,
		     FuGoodixtpFirmware)

gboolean
fu_goodixtp_brlb_firmware_parse(FuGoodixtpFirmware *self,
				GBytes *fw,
				guint8 sensor_id,
				GError **error);
FuFirmware *
fu_goodixtp_brlb_firmware_new(void);
