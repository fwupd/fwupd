/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_CAPE_FIRMWARE (fu_synaptics_cape_firmware_get_type())

G_DECLARE_DERIVABLE_TYPE(FuSynapticsCapeFirmware,
			 fu_synaptics_cape_firmware,
			 FU,
			 SYNAPTICS_CAPE_FIRMWARE,
			 FuFirmware)

struct _FuSynapticsCapeFirmwareClass {
	FuFirmwareClass parent_class;
};

guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self);
void
fu_synaptics_cape_firmware_set_vid(FuSynapticsCapeFirmware *self, guint16 vid);
guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self);
void
fu_synaptics_cape_firmware_set_pid(FuSynapticsCapeFirmware *self, guint16 pid);
