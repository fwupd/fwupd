/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-st-stm32-device.h"
#include "fu-st-stm32-plugin.h"

struct _FuStStm32Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuStStm32Plugin, fu_st_stm32_plugin, FU_TYPE_PLUGIN)

static void
fu_st_stm32_plugin_init(FuStStm32Plugin *self)
{
}

static void
fu_st_stm32_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "StStm32SramAddr");
	fu_context_add_quirk_key(ctx, "StStm32SramLen");
	fu_context_add_quirk_key(ctx, "StStm32FlashAddr");
	fu_context_add_quirk_key(ctx, "StStm32FlashLen");
	fu_context_add_quirk_key(ctx, "StStm32PagesPerSector");
	fu_context_add_quirk_key(ctx, "StStm32PageSize");
	fu_context_add_quirk_key(ctx, "StStm32OptionAddr");
	fu_context_add_quirk_key(ctx, "StStm32OptionLen");
	fu_context_add_quirk_key(ctx, "StStm32MemAddr");
	fu_context_add_quirk_key(ctx, "StStm32MemLen");
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ST_STM32_DEVICE);
	G_OBJECT_CLASS(fu_st_stm32_plugin_parent_class)->constructed(obj);
}

static void
fu_st_stm32_plugin_class_init(FuStStm32PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_st_stm32_plugin_constructed;
}
