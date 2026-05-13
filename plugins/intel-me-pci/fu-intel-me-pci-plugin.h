/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_ME_PCI_PLUGIN (fu_intel_me_pci_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMePciPlugin, fu_intel_me_pci_plugin, FU, INTEL_ME_PCI_PLUGIN, FuPlugin)
