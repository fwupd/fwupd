/*
 * Copyright (C) 2023 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-synaptics-cape-firmware.h"

#define FU_TYPE_SYNAPTICS_CAPE_SNGL_FIRMWARE (fu_synaptics_cape_sngl_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuSynapticsCapeSnglFirmware,
		     fu_synaptics_cape_sngl_firmware,
		     FU,
		     SYNAPTICS_CAPE_SNGL_FIRMWARE,
		     FuSynapticsCapeFirmware)
