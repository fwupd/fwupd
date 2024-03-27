/*
 * Copyright 2005 Synaptics Incorporated
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-synaptics-cxaudio-struct.h"

#define FU_TYPE_SYNAPTICS_CXAUDIO_FIRMWARE (fu_synaptics_cxaudio_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsCxaudioFirmware,
		     fu_synaptics_cxaudio_firmware,
		     FU,
		     SYNAPTICS_CXAUDIO_FIRMWARE,
		     FuSrecFirmware)

FuFirmware *
fu_synaptics_cxaudio_firmware_new(void);
FuSynapticsCxaudioFileKind
fu_synaptics_cxaudio_firmware_get_file_type(FuSynapticsCxaudioFirmware *self);
FuSynapticsCxaudioDeviceKind
fu_synaptics_cxaudio_firmware_get_devtype(FuSynapticsCxaudioFirmware *self);
guint8
fu_synaptics_cxaudio_firmware_get_layout_version(FuSynapticsCxaudioFirmware *self);
