/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-dmc-device.h"
#include "fu-ccgx-dmc-firmware.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hid-device.h"
#include "fu-ccgx-hpi-device.h"
#include "fu-ccgx-plugin.h"

struct _FuCcgxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCcgxPlugin, fu_ccgx_plugin, FU_TYPE_PLUGIN)

static void
fu_ccgx_plugin_init(FuCcgxPlugin *self)
{
}

static void
fu_ccgx_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "CcgxFlashRowSize");
	fu_context_add_quirk_key(ctx, "CcgxFlashSize");
	fu_context_add_quirk_key(ctx, "CcgxImageKind");
	fu_context_add_quirk_key(ctx, "CcgxDmcTriggerCode");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CCGX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CCGX_DMC_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_HPI_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_DMC_DEVICE);
}

static void
fu_ccgx_plugin_class_init(FuCcgxPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_ccgx_plugin_constructed;
}
