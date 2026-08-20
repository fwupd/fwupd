/*
 * Copyright 2025 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REDFISH_NVIDIA_FIRMWARE (fu_redfish_nvidia_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishNvidiaFirmware,
		     fu_redfish_nvidia_firmware,
		     FU,
		     REDFISH_NVIDIA_FIRMWARE,
		     FuFirmware)
