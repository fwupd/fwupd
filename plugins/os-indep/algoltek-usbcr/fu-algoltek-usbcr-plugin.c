/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usbcr-device.h"
#include "fu-algoltek-usbcr-firmware.h"
#include "fu-algoltek-usbcr-plugin.h"

struct _FuAlgoltekUsbcrPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbcrPlugin, fu_algoltek_usbcr_plugin, FU_TYPE_PLUGIN)

static void
fu_algoltek_usbcr_plugin_init(FuAlgoltekUsbcrPlugin *self)
{
}

static void
fu_algoltek_usbcr_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "block:disk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ALGOLTEK_USBCR_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ALGOLTEK_USBCR_FIRMWARE);
}

static void
fu_algoltek_usbcr_plugin_class_init(FuAlgoltekUsbcrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_algoltek_usbcr_plugin_constructed;
}
