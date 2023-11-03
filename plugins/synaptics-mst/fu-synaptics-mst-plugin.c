/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-mst-device.h"
#include "fu-synaptics-mst-firmware.h"
#include "fu-synaptics-mst-plugin.h"

struct _FuSynapticsMstPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsMstPlugin, fu_synaptics_mst_plugin, FU_TYPE_PLUGIN)

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
	g_autoptr(GError) error_local = NULL;

	/* interesting device? */
	if (!FU_IS_DPAUX_DEVICE(device))
		return TRUE;

	/* for SynapticsMstDeviceKind=system devices */
	dev = fu_synaptics_mst_device_new(FU_DPAUX_DEVICE(device));
	fu_synaptics_mst_device_set_system_type(
	    dev,
	    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU));

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
fu_synaptics_mst_plugin_class_init(FuSynapticsMstPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_synaptics_mst_plugin_constructed;
	plugin_class->write_firmware = fu_synaptics_mst_plugin_write_firmware;
	plugin_class->backend_device_added = fu_synaptics_mst_plugin_backend_device_added;
}
