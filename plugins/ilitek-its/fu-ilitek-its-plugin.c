/*
 * Copyright 2025 Joe Hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-device.h"
#include "fu-ilitek-its-firmware.h"
#include "fu-ilitek-its-plugin.h"

struct _FuIlitekItsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIlitekItsPlugin, fu_ilitek_its_plugin, FU_TYPE_PLUGIN)

static void
fu_ilitek_its_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* found DRM device for querying edid */
	if (FU_IS_DRM_DEVICE(device))
		fu_plugin_cache_add(plugin, "drm", device);
}

static gboolean
fu_ilitek_its_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDevice *drm_device = fu_plugin_cache_lookup(plugin, "drm");

	if (drm_device != NULL)
		fu_device_add_child(device, drm_device);

	return TRUE;
}

static void
fu_ilitek_its_plugin_init(FuIlitekItsPlugin *self)
{
}

static void
fu_ilitek_its_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	//	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_udev_subsystem(plugin, "drm");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ILITEK_ITS_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ILITEK_ITS_FIRMWARE);
}

static void
fu_ilitek_its_plugin_class_init(FuIlitekItsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_ilitek_its_plugin_constructed;
	plugin_class->device_created = fu_ilitek_its_plugin_device_created;
	plugin_class->device_registered = fu_ilitek_its_plugin_device_registered;
}
