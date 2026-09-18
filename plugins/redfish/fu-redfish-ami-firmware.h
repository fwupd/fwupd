/*
 * Copyright 2025 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REDFISH_AMI_FIRMWARE (fu_redfish_ami_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishAmiFirmware,
		     fu_redfish_ami_firmware,
		     FU,
		     REDFISH_AMI_FIRMWARE,
		     FuFirmware)
