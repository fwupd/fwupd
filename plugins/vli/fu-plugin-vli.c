/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_VLI_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_VLI_PD_FIRMWARE);
	fu_plugin_add_possible_quirk_key (plugin, "VliDeviceKind");
	fu_plugin_add_possible_quirk_key (plugin, "VliSpiAutoDetect");
	fu_plugin_add_possible_quirk_key (plugin, "VliSpiCmdChipErase");
	fu_plugin_add_possible_quirk_key (plugin, "VliSpiCmdReadId");
	fu_plugin_add_possible_quirk_key (plugin, "VliSpiCmdReadIdSz");
	fu_plugin_add_possible_quirk_key (plugin, "VliSpiCmdSectorErase");

	/* register the custom types */
	g_type_ensure (FU_TYPE_VLI_USBHUB_DEVICE);
	g_type_ensure (FU_TYPE_VLI_PD_DEVICE);
}
