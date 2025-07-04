/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-kbd-debug-device.h"
#include "fu-elan-kbd-device.h"
#include "fu-elan-kbd-firmware.h"
#include "fu-elan-kbd-plugin.h"
#include "fu-elan-kbd-runtime.h"

struct _FuElanKbdPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuElanKbdPlugin, fu_elan_kbd_plugin, FU_TYPE_PLUGIN)

static void
fu_elan_kbd_plugin_init(FuElanKbdPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_elan_kbd_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELAN_KBD_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ELAN_KBD_DEBUG_DEVICE);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_ELAN_KBD_RUNTIME);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ELAN_KBD_FIRMWARE);
}

static void
fu_elan_kbd_plugin_class_init(FuElanKbdPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_elan_kbd_plugin_constructed;
}
