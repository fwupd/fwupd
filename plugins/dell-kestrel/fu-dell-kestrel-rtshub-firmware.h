/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_RTSHUB_FIRMWARE (fu_dell_kestrel_rtshub_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelRtshubFirmware,
		     fu_dell_kestrel_rtshub_firmware,
		     FU,
		     DELL_KESTREL_RTSHUB_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_dell_kestrel_rtshub_firmware_new(void);
