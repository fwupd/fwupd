/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-device.h"
#include "fu-synaptics-mst-firmware.h"
#include "fu-synaptics-mst-plugin.h"

#define FU_SYNAPTICS_MST_DRM_REPLUG_DELAY 5 /* s */

struct _FuSynapticsMstPlugin {
	FuPlugin parent_instance;
	GPtrArray *devices;
	guint drm_changed_id;
};

G_DEFINE_TYPE(FuSynapticsMstPlugin, fu_synaptics_mst_plugin, FU_TYPE_PLUGIN)

static void
fu_synaptics_mst_plugin_device_rescan(FuSynapticsMstPlugin *self, FuDevice *device)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* open fd */
	locker = fu_device_locker_new(device, &error_local);
	if (locker == NULL) {
		g_debug("failed to open device %s: %s",
			fu_device_get_logical_id(device),
			error_local->message);
		return;
	}
	if (!fu_device_rescan(device, &error_local)) {
		g_debug("no device found on %s: %s",
			fu_device_get_logical_id(device),
			error_local->message);
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REGISTERED))
			fu_plugin_device_remove(FU_PLUGIN(self), device);
	} else {
		fu_plugin_device_add(FU_PLUGIN(self), device);
	}
}

/* reprobe all existing devices added by this plugin */
static void
fu_synaptics_mst_plugin_rescan(FuSynapticsMstPlugin *self)
{
	for (guint i = 0; i < self->devices->len; i++) {
		FuDevice *device = FU_DEVICE(g_ptr_array_index(self->devices, i));
		fu_synaptics_mst_plugin_device_rescan(self, device);
	}
}

static gboolean
fu_synaptics_mst_plugin_rescan_cb(gpointer user_data)
{
	FuSynapticsMstPlugin *self = FU_SYNAPTICS_MST_PLUGIN(user_data);
	fu_synaptics_mst_plugin_rescan(self);
	self->drm_changed_id = 0;
	return FALSE;
}

static gboolean
fu_synaptics_mst_plugin_backend_device_changed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuSynapticsMstPlugin *self = FU_SYNAPTICS_MST_PLUGIN(plugin);

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "drm") != 0)
		return TRUE;

	/* recoldplug all drm_dp_aux_dev devices after a *long* delay */
	if (self->drm_changed_id != 0)
		g_source_remove(self->drm_changed_id);
	self->drm_changed_id = g_timeout_add_seconds(FU_SYNAPTICS_MST_DRM_REPLUG_DELAY,
						     fu_synaptics_mst_plugin_rescan_cb,
						     plugin);
	return TRUE;
}

static gboolean
fu_synaptics_mst_plugin_backend_device_added(FuPlugin *plugin,
					     FuDevice *device,
					     FuProgress *progress,
					     GError **error)
{
	FuSynapticsMstPlugin *self = FU_SYNAPTICS_MST_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuSynapticsMstDevice) dev = NULL;

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "open");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 99, "rescan");

	dev = fu_synaptics_mst_device_new(FU_UDEV_DEVICE(device));
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* for SynapticsMstDeviceKind=system devices */
	fu_synaptics_mst_device_set_system_type(
	    FU_SYNAPTICS_MST_DEVICE(dev),
	    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU));

	/* this might fail if there is nothing connected */
	fu_synaptics_mst_plugin_device_rescan(self, FU_DEVICE(dev));
	g_ptr_array_add(self->devices, g_steal_pointer(&dev));
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_synaptics_mst_plugin_write_firmware(FuPlugin *plugin,
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
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART))
		fu_plugin_device_remove(plugin, device);
	return TRUE;
}

static void
fu_synaptics_mst_plugin_init(FuSynapticsMstPlugin *self)
{
	/* devices added by this plugin */
	self->devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_synaptics_mst_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "SynapticsMstDeviceKind");
	fu_plugin_add_udev_subsystem(plugin, "drm"); /* used for uevent only */
	fu_plugin_add_device_udev_subsystem(plugin, "drm_dp_aux_dev");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_MST_FIRMWARE);
}

static void
fu_synaptics_mst_finalize(GObject *obj)
{
	FuSynapticsMstPlugin *self = FU_SYNAPTICS_MST_PLUGIN(obj);
	if (self->drm_changed_id != 0)
		g_source_remove(self->drm_changed_id);
	g_ptr_array_unref(self->devices);
	G_OBJECT_CLASS(fu_synaptics_mst_plugin_parent_class)->finalize(obj);
}

static void
fu_synaptics_mst_plugin_class_init(FuSynapticsMstPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_synaptics_mst_finalize;
	plugin_class->constructed = fu_synaptics_mst_plugin_constructed;
	plugin_class->write_firmware = fu_synaptics_mst_plugin_write_firmware;
	plugin_class->backend_device_added = fu_synaptics_mst_plugin_backend_device_added;
	plugin_class->backend_device_changed = fu_synaptics_mst_plugin_backend_device_changed;
}
