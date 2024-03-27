/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-wacom-aes-device.h"
#include "fu-wacom-common.h"
#include "fu-wacom-emr-device.h"
#include "fu-wacom-raw-plugin.h"

struct _FuWacomRawPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWacomRawPlugin, fu_wacom_raw_plugin, FU_TYPE_PLUGIN)

static void
fu_wacom_raw_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* is internal DRM device */
	if (FU_IS_DRM_DEVICE(device) && fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		GPtrArray *devices = fu_plugin_get_devices(plugin);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			fu_device_add_child(device, device_tmp);
		}
		fu_plugin_cache_add(plugin, "drm", device);
	}
}

static gboolean
fu_wacom_raw_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDevice *drm_device = fu_plugin_cache_lookup(plugin, "drm");
	if (drm_device != NULL)
		fu_device_add_child(drm_device, device);
	return TRUE;
}

static void
fu_wacom_raw_plugin_init(FuWacomRawPlugin *self)
{
}

static void
fu_wacom_raw_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "WacomI2cFlashBlockSize");
	fu_context_add_quirk_key(ctx, "WacomI2cFlashBaseAddr");
	fu_context_add_quirk_key(ctx, "WacomI2cFlashSize");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_AES_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_EMR_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_wacom_raw_plugin_class_init(FuWacomRawPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_wacom_raw_plugin_constructed;
	plugin_class->device_created = fu_wacom_raw_plugin_device_created;
	plugin_class->device_registered = fu_wacom_raw_plugin_device_registered;
}
