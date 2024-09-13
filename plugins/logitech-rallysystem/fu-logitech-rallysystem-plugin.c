/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-rallysystem-audio-device.h"
#include "fu-logitech-rallysystem-plugin.h"
#include "fu-logitech-rallysystem-tablehub-device.h"

struct _FuLogitechRallysystemPlugin {
	FuPlugin parent_instance;
	gchar *system_version;
};

G_DEFINE_TYPE(FuLogitechRallysystemPlugin, fu_logitech_rallysystem_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_rallysystem_plugin_init(FuLogitechRallysystemPlugin *self)
{
}

static void
fu_logitech_rallysystem_plugin_finalize(GObject *object)
{
	FuLogitechRallysystemPlugin *self = FU_LOGITECH_RALLYSYSTEM_PLUGIN(object);
	g_free(self->system_version);
	G_OBJECT_CLASS(fu_logitech_rallysystem_plugin_parent_class)->finalize(object);
}

static void
fu_logitech_rallysystem_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuLogitechRallysystemPlugin *self = FU_LOGITECH_RALLYSYSTEM_PLUGIN(plugin);
	GPtrArray *devices;

	/*
	 * composite device is composed of multiple sub-devices: audio, video, tablehub, speakers.
	 * Each sub-device has their own unique firmware version.
	 * Only audio sub-device has topology information of all sub-devices.
	 * Only audio sub-device has information of overall/unique system version.
	 * Only tablehub device is responsible to push firmware images to all sub-devices,
	 * all sub-devices update firmware independently, and provide status to tablehub.
	 * Since only tablehub can accept firmware image, its guid is used in .metainfo.xml file.
	 * Ask here is for tablehub, to provide system version, so that application can use single
	 * guid to query system version and check .metainfo.xml to determine if upgraded is needed
	 * or not. Following logic preserve the system version information from audio sub-device and
	 * overwrite local version information of tablehub with this system version
	 */
	if (g_strcmp0(fu_device_get_plugin(device), "logitech_rallysystem") == 0) {
		if (FU_IS_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE(device))
			self->system_version = strdup(fu_device_get_version(device));

		devices = fu_plugin_get_devices(plugin);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			if (FU_IS_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device_tmp)) {
				fu_device_set_version(device_tmp, self->system_version);
				g_debug("overwriting tablehub version to %s", self->system_version);
				break;
			}
		}
	}
}

static void
fu_logitech_rallysystem_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE);
}

static void
fu_logitech_rallysystem_plugin_class_init(FuLogitechRallysystemPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logitech_rallysystem_plugin_finalize;
	plugin_class->constructed = fu_logitech_rallysystem_plugin_constructed;
	plugin_class->device_added = fu_logitech_rallysystem_plugin_device_added;
}
