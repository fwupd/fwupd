/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-genesys-plugin.h"
#include "fu-genesys-scaler-firmware.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"

struct _FuGenesysPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGenesysPlugin, fu_genesys_plugin, FU_TYPE_PLUGIN)

static void
fu_genesys_plugin_init(FuGenesysPlugin *self)
{
}

static void
fu_genesys_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "GenesysScalerCfiFlashId");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioOutputRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioEnableRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioValue");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubReadRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubSwitchRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubWriteRequest");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GENESYS_USBHUB_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_GENESYS_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_GENESYS_SCALER_FIRMWARE);
}

static void
fu_genesys_plugin_class_init(FuGenesysPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_genesys_plugin_constructed;
}
