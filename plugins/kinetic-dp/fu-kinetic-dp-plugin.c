/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.kinetic.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-common.h"
#include "fu-kinetic-dp-device.h"
#include "fu-kinetic-dp-firmware.h"
#include "fu-kinetic-dp-plugin.h"

#define FU_KINETIC_DP_DRM_REPLUG_DELAY 5 /* Unit: sec */

struct _FuKineticDpPlugin {
	FuPlugin parent_instance;
	GPtrArray *devices;
	guint drm_changed_id;
};

G_DEFINE_TYPE(FuKineticDpPlugin, fu_kinetic_dp_plugin, FU_TYPE_PLUGIN)

/* start scanning for device existence */
static void
fu_kinetic_dp_plugin_device_rescan(FuPlugin *plugin, FuDevice *device)
{
	const gchar *device_logical_id = fu_device_get_logical_id(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* open fd */
	g_debug("plugin scanning device logical id: %s\n", device_logical_id);
	locker = fu_device_locker_new(device, &error_local);
	if (locker == NULL) {
		g_debug("failed to open device %s: %s", device_logical_id, error_local->message);
		return;
	}
	/* scan device and add if found */
	if (!fu_device_rescan(device, &error_local)) {
		g_debug("no device found with id %s: %s", device_logical_id, error_local->message);
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REGISTERED)) {
			g_debug("plugin removed device id %s", device_logical_id);
			fu_plugin_device_remove(plugin, device);
		}
	} else {
		fu_plugin_device_add(plugin, device);
		g_debug("plugin added device id %s", device_logical_id);
	}
}

/* reprobe all existing devices added by this plugin */
static void
fu_kinetic_dp_plugin_rescan(FuPlugin *plugin)
{
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(plugin);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDevice *device = FU_DEVICE(g_ptr_array_index(self->devices, i));
		fu_kinetic_dp_plugin_device_rescan(plugin, device);
	}
}

/* rescan after device changed (cold-plug) */
static gboolean
fu_kinetic_dp_plugin_rescan_cb(gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(plugin);
	fu_kinetic_dp_plugin_rescan(plugin);
	self->drm_changed_id = 0;
	return FALSE;
}

/* plugin device changes entry point */
static gboolean
fu_kinetic_dp_plugin_backend_device_changed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(plugin);

	/* check to see if this is device we care about? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;

	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "drm") != 0)
		return TRUE;

	/* cold-plug again all drm_dp_aux_dev devices after a *long* delay */
	if (self->drm_changed_id != 0)
		g_source_remove(self->drm_changed_id);

	self->drm_changed_id = g_timeout_add_seconds(FU_KINETIC_DP_DRM_REPLUG_DELAY,
						     fu_kinetic_dp_plugin_rescan_cb,
						     plugin);
	return TRUE;
}

/* plugin entry point 3 to be call during scanning process */
static gboolean
fu_kinetic_dp_plugin_backend_device_added(FuPlugin *plugin,
					  FuDevice *device,
					  FuProgress *progress,
					  GError **error)
{
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuKineticDpDevice) dev = NULL;

	g_debug("plugin adding backend device...");
	/* check to see if this is device we care about? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;

	/* instantiate a new device */
	dev = fu_kinetic_dp_device_new(FU_UDEV_DEVICE(device));
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;

	/* for DeviceKind=system devices */
	fu_kinetic_dp_device_set_system_type(
	    FU_KINETIC_DP_DEVICE(dev),
	    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU));

	/* get fd and scan, it might fail if there is nothing connected */
	fu_kinetic_dp_plugin_device_rescan(plugin, FU_DEVICE(dev));

	/* add device to list to keep track */
	g_ptr_array_add(self->devices, g_steal_pointer(&dev));

	return TRUE;
}

/* plugin starting point of firmware action */
static gboolean
fu_kinetic_dp_plugin_write_firmware(FuPlugin *plugin,
				    FuDevice *device,
				    GBytes *blob_fw,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);

	if (locker == NULL)
		return FALSE;
	if (!fu_device_write_firmware(device, blob_fw, progress, flags, error))
		return FALSE;
	return TRUE;
}

static void
fu_kinetic_dp_plugin_init(FuKineticDpPlugin *self)
{
	/* devices added by this plugin */
	self->devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_kinetic_dp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "drm"); /* used for uevent only */
	fu_plugin_add_udev_subsystem(plugin, "drm_dp_aux_dev");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_KINETIC_DP_FIRMWARE);
}

static void
fu_kinetic_dp_plugin_finalize(GObject *obj)
{
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(obj);
	if (self->drm_changed_id != 0)
		g_source_remove(self->drm_changed_id);
	g_ptr_array_unref(self->devices);
	G_OBJECT_CLASS(fu_kinetic_dp_plugin_parent_class)->finalize(obj);
}

static void
fu_kinetic_dp_plugin_class_init(FuKineticDpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_kinetic_dp_plugin_constructed;
	object_class->finalize = fu_kinetic_dp_plugin_finalize;
	plugin_class->write_firmware = fu_kinetic_dp_plugin_write_firmware;
	plugin_class->backend_device_added = fu_kinetic_dp_plugin_backend_device_added;
	plugin_class->backend_device_changed = fu_kinetic_dp_plugin_backend_device_changed;
}
