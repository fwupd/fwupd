/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-prometheus-config.h"
#include "fu-synaptics-prometheus-device.h"
#include "fu-synaptics-prometheus-firmware.h"
#include "fu-synaptics-prometheus-plugin.h"

struct _FuSynapticsPrometheusPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsPrometheusPlugin, fu_synaptics_prometheus_plugin, FU_TYPE_PLUGIN)

static void
fu_synaptics_prometheus_plugin_init(FuSynapticsPrometheusPlugin *self)
{
}

static void
fu_synaptics_prometheus_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_SYNAPTICS_PROMETHEUS_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_PROMETHEUS_CONFIG); /* for coverage */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_SYNAPTICS_PROMETHEUS_FIRMWARE);
}

static void
fu_synaptics_prometheus_plugin_class_init(FuSynapticsPrometheusPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_synaptics_prometheus_plugin_constructed;
}
