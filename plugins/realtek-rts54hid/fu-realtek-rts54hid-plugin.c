/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-rts54hid-hub-device.h"
#include "fu-realtek-rts54hid-module.h"
#include "fu-realtek-rts54hid-pd-device.h"
#include "fu-realtek-rts54hid-plugin.h"

struct _FuRealtekRts54hidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRealtekRts54hidPlugin, fu_realtek_rts54hid_plugin, FU_TYPE_PLUGIN)

static void
fu_realtek_rts54hid_plugin_init(FuRealtekRts54hidPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_realtek_rts54hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key(ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key(ctx, "Rts54RegisterAddrLen");
	fu_context_add_quirk_key(ctx, "Rts54HidType");
	fu_context_add_quirk_key(ctx, "Rts54ResetAfterUpdate");
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_REALTEK_RTS54HID_HUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_REALTEK_RTS54HID_PD_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_REALTEK_RTS54HID_MODULE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_realtek_rts54hid_plugin_parent_class)->constructed(obj);
}

static void
fu_realtek_rts54hid_plugin_class_init(FuRealtekRts54hidPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_realtek_rts54hid_plugin_constructed;
}
