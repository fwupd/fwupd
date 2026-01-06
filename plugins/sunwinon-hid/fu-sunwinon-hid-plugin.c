/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-sunwinon-ble-hid-device.h"

typedef struct {
	FuPlugin parent_instance;
} FuSunwinonHidPlugin;

typedef struct {
	FuPluginClass parent_class;
} FuSunwinonHidPluginClass;

G_DEFINE_TYPE(FuSunwinonHidPlugin, fu_sunwinon_hid_plugin, FU_TYPE_PLUGIN)

static void
fu_sunwinon_hid_plugin_init(FuSunwinonHidPlugin *self)
{
}

static void
fu_sunwinon_hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SUNWINON_BLE_HID_DEVICE);
}

static void
fu_sunwinon_hid_plugin_class_init(FuSunwinonHidPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_sunwinon_hid_plugin_constructed;
}
