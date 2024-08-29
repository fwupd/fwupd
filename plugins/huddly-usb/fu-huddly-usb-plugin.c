/*
 * Copyright 2024 Huddly
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-huddly-usb-device.h"
#include "fu-huddly-usb-plugin.h"

struct _FuHuddlyUsbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHuddlyUsbPlugin, fu_huddly_usb_plugin, FU_TYPE_PLUGIN)

static void
fu_huddly_usb_plugin_init(FuHuddlyUsbPlugin *self)
{
}

static void
fu_huddly_usb_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HUDDLY_USB_DEVICE);
}

static void
fu_huddly_usb_plugin_class_init(FuHuddlyUsbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_huddly_usb_plugin_constructed;
}
