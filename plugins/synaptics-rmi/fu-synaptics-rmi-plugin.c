/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-hid-device.h"
#include "fu-synaptics-rmi-plugin.h"
#include "fu-synaptics-rmi-ps2-device.h"

struct _FuSynapticsRmiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsRmiPlugin, fu_synaptics_rmi_plugin, FU_TYPE_PLUGIN)

static void
fu_synaptics_rmi_plugin_init(FuSynapticsRmiPlugin *self)
{
}

static void
fu_synaptics_rmi_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_udev_subsystem(plugin, "serio");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_RMI_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_RMI_PS2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_RMI_FIRMWARE);
}

static void
fu_synaptics_rmi_plugin_class_init(FuSynapticsRmiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_synaptics_rmi_plugin_constructed;
}
