/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-test-ble-device.h"
#include "fu-test-ble-plugin.h"

struct _FuTestBlePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuTestBlePlugin, fu_test_ble_plugin, FU_TYPE_PLUGIN)

static void
fu_test_ble_plugin_init(FuTestBlePlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_TEST_ONLY);
}

static void
fu_test_ble_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TEST_BLE_DEVICE);
}

static void
fu_test_ble_plugin_class_init(FuTestBlePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_test_ble_plugin_constructed;
}
