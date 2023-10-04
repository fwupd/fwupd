/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
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
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_pci_psp_plugin_constructed;
}
