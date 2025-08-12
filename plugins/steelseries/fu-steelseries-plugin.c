/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-gen1.h"
#include "fu-steelseries-fizz-gen2.h"
#include "fu-steelseries-fizz-hid.h"
#include "fu-steelseries-fizz-tunnel.h"
#include "fu-steelseries-fizz.h"
#include "fu-steelseries-gamepad.h"
#include "fu-steelseries-mouse.h"
#include "fu-steelseries-plugin.h"
#include "fu-steelseries-sonic.h"

struct _FuSteelseriesPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesPlugin, fu_steelseries_plugin, FU_TYPE_PLUGIN)

static void
fu_steelseries_plugin_init(FuSteelseriesPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_steelseries_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "SteelSeriesCmdInterface");
	fu_context_add_quirk_key(ctx, "SteelSeriesFizzProtocolRevision");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_GEN1);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_GEN2);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_HID);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_TUNNEL);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_GAMEPAD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_MOUSE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_SONIC);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static FuDevice *
fu_steelseries_plugin_find_device_by_serial(FuSteelseriesPlugin *self, const gchar *serial)
{
	GPtrArray *devices = fu_plugin_get_devices(FU_PLUGIN(self));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (g_strcmp0(serial, fu_device_get_serial(device)) == 0)
			return device;
	}
	return NULL;
}

static void
fu_steelseries_plugin_device_added_all(FuSteelseriesPlugin *self, FuDevice *device)
{
	if (fu_device_get_serial(device) != NULL) {
		FuDevice *device2 =
		    fu_steelseries_plugin_find_device_by_serial(self, fu_device_get_serial(device));
		if (device2 != NULL && device != device2)
			fu_device_set_equivalent_id(device, fu_device_get_id(device2));
	}
}

static void
fu_steelseries_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuSteelseriesPlugin *self = FU_STEELSERIES_PLUGIN(plugin);
	GPtrArray *children = fu_device_get_children(device);

	/* parent then children */
	fu_steelseries_plugin_device_added_all(self, device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		fu_steelseries_plugin_device_added_all(self, child);
	}
}

static void
fu_steelseries_plugin_class_init(FuSteelseriesPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_steelseries_plugin_constructed;
	plugin_class->device_added = fu_steelseries_plugin_device_added;
}
