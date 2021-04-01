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
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_VLI_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_VLI_PD_FIRMWARE);
	fu_context_add_quirk_key (ctx, "VliDeviceKind");
	fu_context_add_quirk_key (ctx, "VliSpiAutoDetect");
	fu_context_add_quirk_key (ctx, "VliSpiCmdChipErase");
	fu_context_add_quirk_key (ctx, "VliSpiCmdReadId");
	fu_context_add_quirk_key (ctx, "VliSpiCmdReadIdSz");
	fu_context_add_quirk_key (ctx, "VliSpiCmdSectorErase");

	/* register the custom types */
	g_type_ensure (FU_TYPE_VLI_USBHUB_DEVICE);
	g_type_ensure (FU_TYPE_VLI_PD_DEVICE);
}
