/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GOODIXTP_BRLB_CONFIG (fu_goodixtp_brlb_config_get_type())

G_DECLARE_FINAL_TYPE(FuGoodixtpBrlbConfig,
		     fu_goodixtp_brlb_config,
		     FU,
		     GOODIXTP_BRLB_CONFIG,
		     FuFirmware)
FuFirmware *
fu_goodixtp_brlb_config_new(void);
