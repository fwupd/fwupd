/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_CAPE_FIRMWARE (fu_synaptics_cape_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuSynapticsCapeFirmware,
		     fu_synaptics_cape_firmware,
		     FU,
		     SYNAPTICS_CAPE_FIRMWARE,
		     FuSrecFirmware)

FuFirmware *
fu_synaptics_cape_firmware_new(void);
guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self);
guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self);
