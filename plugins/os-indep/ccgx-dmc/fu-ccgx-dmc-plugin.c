/*
 * Copyright 2020 Cypress Semiconductor Corporation.
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ccgx-dmc-device.h"
#include "fu-ccgx-dmc-devx-device.h"
#include "fu-ccgx-dmc-firmware.h"
#include "fu-ccgx-dmc-plugin.h"

struct _FuCcgxDmcPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCcgxDmcPlugin, fu_ccgx_dmc_plugin, FU_TYPE_PLUGIN)

static void
fu_ccgx_dmc_plugin_init(FuCcgxDmcPlugin *self)
{
}

static void
fu_ccgx_dmc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "CcgxDmcTriggerCode");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CCGX_DMC_FIRMWARE);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_CCGX_DMC_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_DMC_DEVX_DEVICE); /* coverage */
}

static void
fu_ccgx_dmc_plugin_class_init(FuCcgxDmcPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_ccgx_dmc_plugin_constructed;
}
