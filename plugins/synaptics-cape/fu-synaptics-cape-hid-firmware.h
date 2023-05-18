/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-synaptics-cape-firmware.h"

#define FU_TYPE_SYNAPTICS_CAPE_HID_FIRMWARE (fu_synaptics_cape_hid_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuSynapticsCapeHidFirmware,
		     fu_synaptics_cape_hid_firmware,
		     FU,
		     SYNAPTICS_CAPE_HID_FIRMWARE,
		     FuSynapticsCapeFirmware)

FuFirmware *
fu_synaptics_cape_hid_firmware_new(void);
