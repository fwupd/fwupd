/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usb-device.h"
#include "fu-algoltek-usb-firmware.h"
#include "fu-algoltek-usb-plugin.h"

struct _FuAlgoltekUsbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbPlugin, fu_algoltek_usb_plugin, FU_TYPE_PLUGIN)

static void
fu_algoltek_usb_plugin_init(FuAlgoltekUsbPlugin *self)
{
}

static void
fu_algoltek_usb_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ALGOLTEK_USB_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ALGOLTEK_USB_FIRMWARE);
}

static void
fu_algoltek_usb_plugin_class_init(FuAlgoltekUsbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_algoltek_usb_plugin_constructed;
}
