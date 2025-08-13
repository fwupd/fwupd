/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-device.h"
#include "fu-ilitek-its-firmware.h"
#include "fu-ilitek-its-plugin.h"

struct _FuIlitekItsPlugin {
	FuPlugin parent_instance;
	GPtrArray *drm_devices;
};

G_DEFINE_TYPE(FuIlitekItsPlugin, fu_ilitek_its_plugin, FU_TYPE_PLUGIN)

static void
fu_ilitek_its_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuIlitekItsPlugin *self = FU_ILITEK_ITS_PLUGIN(plugin);

	/* new DRM device, so register for any added FuIlitekItsDevices */
	if (FU_IS_DRM_DEVICE(device)) {
		GPtrArray *its_devices = fu_plugin_get_devices(plugin);
		for (guint i = 0; i < its_devices->len; i++) {
			FuIlitekItsDevice *its_device = g_ptr_array_index(its_devices, i);
			FuDrmDevice *drm_device = FU_DRM_DEVICE(device);
			g_autoptr(GError) error_local = NULL;

			if (!fu_ilitek_its_device_register_drm_device(its_device,
								      drm_device,
								      &error_local)) {
				g_warning("ignoring: %s", error_local->message);
				continue;
			}
		}
		g_ptr_array_add(self->drm_devices, g_object_ref(device));
	}
}

static gboolean
fu_ilitek_its_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuIlitekItsPlugin *self = FU_ILITEK_ITS_PLUGIN(plugin);
	FuIlitekItsDevice *its_device = FU_ILITEK_ITS_DEVICE(device);

	/* any DRM devices added before ITS devices */
	for (guint i = 0; i < self->drm_devices->len; i++) {
		FuDrmDevice *drm_device = g_ptr_array_index(self->drm_devices, i);
		if (!fu_ilitek_its_device_register_drm_device(its_device, drm_device, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_ilitek_its_plugin_init(FuIlitekItsPlugin *self)
{
	self->drm_devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_ilitek_its_plugin_finalize(GObject *object)
{
	FuIlitekItsPlugin *self = FU_ILITEK_ITS_PLUGIN(object);
	g_ptr_array_unref(self->drm_devices);
	G_OBJECT_CLASS(fu_ilitek_its_plugin_parent_class)->finalize(object);
}

static void
fu_ilitek_its_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ILITEK_ITS_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ILITEK_ITS_FIRMWARE);
}

static void
fu_ilitek_its_plugin_class_init(FuIlitekItsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_ilitek_its_plugin_finalize;
	plugin_class->constructed = fu_ilitek_its_plugin_constructed;
	plugin_class->device_created = fu_ilitek_its_plugin_device_created;
	plugin_class->device_registered = fu_ilitek_its_plugin_device_registered;
}
