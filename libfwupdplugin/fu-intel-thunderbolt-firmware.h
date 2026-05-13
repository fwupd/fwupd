/*
 * Copyright 2021 Dell Inc.
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-intel-thunderbolt-nvm.h"

#define FU_TYPE_INTEL_THUNDERBOLT_FIRMWARE (fu_intel_thunderbolt_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIntelThunderboltFirmware,
			 fu_intel_thunderbolt_firmware,
			 FU,
			 INTEL_THUNDERBOLT_FIRMWARE,
			 FuIntelThunderboltNvm)

struct _FuIntelThunderboltFirmwareClass {
	FuIntelThunderboltNvmClass parent_class;
};

FuFirmware *
fu_intel_thunderbolt_firmware_new(void);
