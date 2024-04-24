/*
 * Copyright (C) 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-algoltek-aux-device.h"
#include "fu-algoltek-aux-firmware.h"
#include "fu-algoltek-aux-plugin.h"

struct _FuAlgoltekAuxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekAuxPlugin, fu_algoltek_aux_plugin, FU_TYPE_PLUGIN)

guint deviceCount = 0;

static gboolean
fu_algoltek_aux_plugin_backend_device_added(FuPlugin *plugin,
					     FuDevice *device,
					     FuProgress *progress,
					     GError **error)
{
	FuAlgoltekAuxPlugin *self = FU_ALGOLTEK_AUX_PLUGIN(plugin);
	//FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuAlgoltekAuxDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	g_print("\ndevice_added: %d\n", deviceCount);

	if (deviceCount < 1) {
		deviceCount += 1;
		/* interesting device? */
		if (!FU_IS_DPAUX_DEVICE(device))
			return TRUE;
	
		/* instantiate a new device */
		dev = fu_algoltek_aux_device_new(FU_DPAUX_DEVICE(device));
		if (dev == NULL)
			return FALSE;
	
		/* open */
		locker = fu_device_locker_new(dev, &error_local);
		if (locker == NULL) {
			if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED) ||
		    	g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
				g_debug("no device found: %s", error_local->message);
				return TRUE;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fu_plugin_device_add(FU_PLUGIN(self), FU_DEVICE(dev));
	
	}
	return TRUE;
}

static void
fu_algoltek_aux_plugin_init(FuAlgoltekAuxPlugin *self)
{
}

static void
fu_algoltek_aux_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	//FuContext *ctx = fu_plugin_get_context(plugin);
	//fu_context_add_quirk_key(ctx, "AlgoltekAuxStartAddr");
	fu_plugin_add_udev_subsystem(plugin, "drm"); /* used for uevent only */
	fu_plugin_add_device_udev_subsystem(plugin, "drm_dp_aux_dev"); //I care about dpaux devices
	//fu_plugin_add_device_gtype(plugin, FU_TYPE_ALGOLTEK_AUX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ALGOLTEK_AUX_FIRMWARE);
}

static void
fu_algoltek_aux_plugin_class_init(FuAlgoltekAuxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_algoltek_aux_plugin_constructed;
	plugin_class->backend_device_added = fu_algoltek_aux_plugin_backend_device_added; //query fu_device
}
