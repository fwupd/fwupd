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
	fu_plugin_add_rule(FU_PLUGIN(self), FU_PLUGIN_RULE_RUN_BEFORE, "thunderbolt");
}

static void
fu_intel_usb4_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_INTEL_USB4_DEVICE);
}

static void
fu_intel_usb4_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	GPtrArray *instance_ids = fu_device_get_instance_ids(device);

	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") != 0)
		return;

	/* prefer using this plugin over the thunderbolt one -- but the device ID is constructed
	 * differently in each plugin as they're using very different update methods.
	 * use the TBT-{nvm_vendor_id}{nvm_product_id} instance ID to match them up instead. */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		for (guint j = 0; j < instance_ids->len; j++) {
			const gchar *instance_id = g_ptr_array_index(instance_ids, j);
			if (g_str_has_prefix(instance_id, "TBT-") &&
			    fu_device_has_instance_id(device_tmp, instance_id)) {
				fu_device_remove_internal_flag(
				    device,
				    FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
				fu_device_inhibit(device,
						  "hidden",
						  "updated by the intel-usb4 plugin instead");
				return;
			}
		}
	}
}

static void
fu_intel_usb4_plugin_class_init(FuIntelUsb4PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_intel_usb4_plugin_constructed;
	plugin_class->device_registered = fu_intel_usb4_plugin_device_registered;
}
