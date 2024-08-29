/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usbcardreader-device.h"
#include "fu-algoltek-usbcardreader-firmware.h"
#include "fu-algoltek-usbcardreader-plugin.h"

struct _FuAlgoltekUsbcardreaderPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbcardreaderPlugin, fu_algoltek_usbcardreader_plugin, FU_TYPE_PLUGIN)

static void
fu_algoltek_usbcardreader_plugin_init(FuAlgoltekUsbcardreaderPlugin *self)
{
}

static void
fu_algoltek_usbcardreader_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "block");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ALGOLTEK_USBCARDREADER_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ALGOLTEK_USBCARDREADER_FIRMWARE);
}

static void
fu_algoltek_usbcardreader_plugin_class_init(FuAlgoltekUsbcardreaderPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_algoltek_usbcardreader_plugin_constructed;
}
