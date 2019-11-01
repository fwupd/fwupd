/*
 * Copyright (C) 2005-2019 Synaptics Incorporated
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-srec-firmware.h"
#include "fu-synaptics-cxaudio-common.h"

#define FU_TYPE_SYNAPTICS_CXAUDIO_FIRMWARE (fu_synaptics_cxaudio_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsCxaudioFirmware, fu_synaptics_cxaudio_firmware, FU, SYNAPTICS_CXAUDIO_FIRMWARE, FuSrecFirmware)

typedef enum {
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_UNKNOWN,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2070X_FW,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2070X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2077X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2076X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2085X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2089X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2098X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2198X_PATCH,
	FU_SYNAPTICS_CXAUDIO_FILE_KIND_LAST
} FuSynapticsCxaudioFileKind;

FuFirmware			*fu_synaptics_cxaudio_firmware_new			(void);
FuSynapticsCxaudioFileKind	 fu_synaptics_cxaudio_firmware_get_file_type		(FuSynapticsCxaudioFirmware	*self);
FuSynapticsCxaudioDeviceKind	 fu_synaptics_cxaudio_firmware_get_devtype		(FuSynapticsCxaudioFirmware	*self);
guint8				 fu_synaptics_cxaudio_firmware_get_layout_version	(FuSynapticsCxaudioFirmware	*self);
