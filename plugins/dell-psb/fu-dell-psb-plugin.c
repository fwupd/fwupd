/*
 * Copyright 2026 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-dell-psb-device.h"
#include "fu-dell-psb-plugin.h"

struct _FuDellPsbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDellPsbPlugin, fu_dell_psb_plugin, FU_TYPE_PLUGIN)

static void
fu_dell_psb_plugin_init(FuDellPsbPlugin *self)
{
}

/* create a device of the given type from a backend device and add it */
static gboolean
fu_dell_psb_plugin_add_device(FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	dev = g_object_new(FU_TYPE_DELL_PSB_DEVICE, "context", fu_plugin_get_context(plugin), NULL);
	fu_device_incorporate(dev, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_add_device(plugin, dev);

	/* success */
	return TRUE;
}

static gboolean
fu_dell_psb_plugin_backend_device_added(FuPlugin *plugin,
					FuDevice *device,
					FuProgress *progress,
					GError **error)
{
	g_autoptr(FuDevice) pci = NULL;

	/* only /dev/sg* nodes can be the switch */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "scsi_generic") != 0)
		return TRUE;

	/* only the Dell PSB switch is interesting on /dev/sg*: it is the PCI
	 * function 1000:00B2, and nothing else on this subsystem is ours */
	pci = fu_device_get_backend_parent_with_subsystem(device, "pci", NULL);
	if (pci == NULL)
		return TRUE;
	if (!fu_device_probe(pci, error))
		return FALSE;
	if (fu_device_get_vid(pci) != 0x1000 || fu_device_get_pid(pci) != 0x00B2)
		return TRUE;

	/* success */
	return fu_dell_psb_plugin_add_device(plugin, device, error);
}

static void
fu_dell_psb_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	fu_plugin_add_device_udev_subsystem(plugin, "scsi_generic");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_PSB_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_dell_psb_plugin_parent_class)->constructed(obj);
}

static void
fu_dell_psb_plugin_class_init(FuDellPsbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dell_psb_plugin_constructed;
	plugin_class->backend_device_added = fu_dell_psb_plugin_backend_device_added;
}
