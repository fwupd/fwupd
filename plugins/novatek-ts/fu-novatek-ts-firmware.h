/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_NOVATEK_TS_FIRMWARE (fu_novatek_ts_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuNovatekTsFirmware,
		     fu_novatek_ts_firmware,
		     FU,
		     NOVATEK_TS_FIRMWARE,
		     FuFirmware)
