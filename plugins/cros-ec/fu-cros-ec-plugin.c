/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cros-ec-firmware.h"
#include "fu-cros-ec-hammer-touchpad-firmware.h"
#include "fu-cros-ec-hammer-touchpad.h"
#include "fu-cros-ec-plugin.h"
#include "fu-cros-ec-usb-device.h"

struct _FuCrosEcPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCrosEcPlugin, fu_cros_ec_plugin, FU_TYPE_PLUGIN)

static void
fu_cros_ec_plugin_init(FuCrosEcPlugin *self)
{
}

static void
fu_cros_ec_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CROS_EC_USB_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CROS_EC_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CROS_EC_HAMMER_TOUCHPAD_FIRMWARE);
}

static void
fu_cros_ec_plugin_class_init(FuCrosEcPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_cros_ec_plugin_constructed;
}
