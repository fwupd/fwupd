/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"

static void
fu_plugin_vli_init(FuPlugin *plugin)
{
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_VLI_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_VLI_PD_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_VLI_USBHUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_VLI_PD_DEVICE);
}

static void
fu_plugin_vli_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "VliDeviceKind");
	fu_context_add_quirk_key(ctx, "VliSpiAutoDetect");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_vli_load;
	vfuncs->init = fu_plugin_vli_init;
}
