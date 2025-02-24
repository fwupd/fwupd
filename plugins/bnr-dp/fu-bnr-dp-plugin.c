/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bnr-dp-device.h"
#include "fu-bnr-dp-firmware.h"
#include "fu-bnr-dp-plugin.h"

struct _FuBnrDpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBnrDpPlugin, fu_bnr_dp_plugin, FU_TYPE_PLUGIN)

static void
fu_bnr_dp_plugin_init(FuBnrDpPlugin *self)
{
}

static void
fu_bnr_dp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "drm");
	fu_plugin_add_device_udev_subsystem(plugin, "drm_dp_aux_dev");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BNR_DP_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BNR_DP_FIRMWARE);
}

static void
fu_bnr_dp_plugin_class_init(FuBnrDpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_bnr_dp_plugin_constructed;
}
