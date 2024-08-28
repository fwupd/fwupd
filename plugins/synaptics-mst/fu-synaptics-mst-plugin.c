/*
 * Copyright 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-device.h"
#include "fu-synaptics-mst-firmware.h"
#include "fu-synaptics-mst-plugin.h"

struct _FuSynapticsMstPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsMstPlugin, fu_synaptics_mst_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_synaptics_mst_plugin_write_firmware(FuPlugin *plugin,
				       FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_write_firmware(device, stream, progress, flags, error))
		return FALSE;
	if (!fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART))
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
	fu_plugin_add_udev_subsystem(plugin, "drm_dp_aux_dev");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_MST_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_MST_FIRMWARE);
}

static void
fu_synaptics_mst_plugin_class_init(FuSynapticsMstPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_synaptics_mst_plugin_constructed;
	plugin_class->write_firmware = fu_synaptics_mst_plugin_write_firmware;
}
