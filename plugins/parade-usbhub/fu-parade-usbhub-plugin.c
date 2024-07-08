/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-parade-usbhub-device.h"
#include "fu-parade-usbhub-firmware.h"
#include "fu-parade-usbhub-plugin.h"

struct _FuParadeUsbhubPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuParadeUsbhubPlugin, fu_parade_usbhub_plugin, FU_TYPE_PLUGIN)

static void
fu_parade_usbhub_plugin_init(FuParadeUsbhubPlugin *self)
{
}

static void
fu_parade_usbhub_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PARADE_USBHUB_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_PARADE_USBHUB_FIRMWARE);
}

static void
fu_parade_usbhub_plugin_class_init(FuParadeUsbhubPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_parade_usbhub_plugin_constructed;
}
