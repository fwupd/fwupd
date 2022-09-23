/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-nordic-hid-archive.h"
#include "fu-nordic-hid-cfg-channel.h"
#include "fu-nordic-hid-firmware-b0.h"
#include "fu-nordic-hid-firmware-mcuboot.h"
#include "fu-nordic-hid-plugin.h"

struct _FuNordicHidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuNordicHidPlugin, fu_nordic_hid_plugin, FU_TYPE_PLUGIN)

static void
fu_nordic_hid_plugin_init(FuNordicHidPlugin *self)
{
}

static void
fu_nordic_hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "NordicHidBootloader");
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NORDIC_HID_CFG_CHANNEL);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NORDIC_HID_ARCHIVE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NORDIC_HID_FIRMWARE_B0);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_NORDIC_HID_FIRMWARE_MCUBOOT);
}

static void
fu_nordic_hid_plugin_class_init(FuNordicHidPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_nordic_hid_plugin_constructed;
}
