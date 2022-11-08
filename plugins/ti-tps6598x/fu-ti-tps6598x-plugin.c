/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-device.h"
#include "fu-ti-tps6598x-firmware.h"
#include "fu-ti-tps6598x-plugin.h"

struct _FuTiTps6598xPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuTiTps6598xPlugin, fu_ti_tps6598x_plugin, FU_TYPE_PLUGIN)

static void
fu_ti_tps6598x_plugin_init(FuTiTps6598xPlugin *self)
{
}

static void
fu_ti_tps6598x_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TI_TPS6598X_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "ti-tps6598x", FU_TYPE_TI_TPS6598X_FIRMWARE);
}

static void
fu_ti_tps6598x_plugin_class_init(FuTiTps6598xPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_ti_tps6598x_plugin_constructed;
}
