/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-pci-psp-device.h"
#include "fu-pci-psp-plugin.h"

struct _FuPciPspPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuPciPspPlugin, fu_pci_psp_plugin, FU_TYPE_PLUGIN)

static void
fu_pci_psp_plugin_init(FuPciPspPlugin *self)
{
}

static void
fu_pci_psp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PCI_PSP_DEVICE);
}

static void
fu_pci_psp_plugin_class_init(FuPciPspPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_pci_psp_plugin_constructed;
}
