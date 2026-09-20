/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Justin Li <justin_li@realsil.com.cn>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-scaler-device.h"
#include "fu-realtek-scaler-plugin.h"

struct _FuRealtekScalerPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRealtekScalerPlugin, fu_realtek_scaler_plugin, FU_TYPE_PLUGIN)

static void
fu_realtek_scaler_plugin_init(FuRealtekScalerPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_realtek_scaler_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "drm");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_REALTEK_SCALER_DEVICE);
	G_OBJECT_CLASS(fu_realtek_scaler_plugin_parent_class)->constructed(obj);
}

static void
fu_realtek_scaler_plugin_class_init(FuRealtekScalerPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_realtek_scaler_plugin_constructed;
}
