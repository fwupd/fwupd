/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-synaptics-cxaudio-device.h"
#include "fu-synaptics-cxaudio-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_SYNAPTICS_CXAUDIO_DEVICE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_SYNAPTICS_CXAUDIO_FIRMWARE);
	fu_plugin_add_possible_quirk_key (plugin, "CxaudioChipIdBase");
	fu_plugin_add_possible_quirk_key (plugin, "CxaudioPatch1ValidAddr");
	fu_plugin_add_possible_quirk_key (plugin, "CxaudioPatch2ValidAddr");
	fu_plugin_add_possible_quirk_key (plugin, "CxaudioSoftwareReset");
}
