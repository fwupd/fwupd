/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-goodixtp-firmware.h"

#define FU_TYPE_GOODIXTP_GTX8_FIRMWARE (fu_goodixtp_gtx8_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuGoodixtpGtx8Firmware,
		     fu_goodixtp_gtx8_firmware,
		     FU,
		     GOODIXTP_GTX8_FIRMWARE,
		     FuGoodixtpFirmware)

gboolean
fu_goodixtp_gtx8_firmware_parse(FuGoodixtpFirmware *self,
				GInputStream *stream,
				guint8 sensor_id,
				GError **error);
FuFirmware *
fu_goodixtp_gtx8_firmware_new(void);
