/*
 * Copyright 2022 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
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
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "cpu");
}

static void
fu_pci_psp_plugin_device_registered(FuPlugin *plugin, FuDevice *dev)
{
	if (g_strcmp0(fu_device_get_plugin(dev), "cpu") == 0)
		fu_plugin_cache_add(plugin, "cpu", dev);
}

static void
fu_pci_psp_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuDevice *cpu_device = fu_plugin_cache_lookup(plugin, "cpu");
	if (cpu_device == NULL)
		return;
	fu_pci_psp_device_set_cpu(FU_PCI_PSP_DEVICE(device), FU_PROCESSOR_DEVICE(cpu_device));
}

static void
fu_pci_psp_plugin_class_init(FuPciPspPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_pci_psp_plugin_constructed;
	plugin_class->device_added = fu_pci_psp_plugin_device_added;
	plugin_class->device_registered = fu_pci_psp_plugin_device_registered;
}
