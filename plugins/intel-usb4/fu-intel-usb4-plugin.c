/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-usb4-device.h"
#include "fu-intel-usb4-plugin.h"

struct _FuIntelUsb4Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelUsb4Plugin, fu_intel_usb4_plugin, FU_TYPE_PLUGIN)

static void
fu_intel_usb4_plugin_init(FuIntelUsb4Plugin *self)
{
}

static void
fu_intel_usb4_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_USB4_DEVICE);
}

static void
fu_intel_usb4_plugin_class_init(FuIntelUsb4PluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_intel_usb4_plugin_constructed;
}
