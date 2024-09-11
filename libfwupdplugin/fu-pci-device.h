/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_PCI_DEVICE (fu_pci_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuPciDevice, fu_pci_device, FU, PCI_DEVICE, FuUdevDevice)

struct _FuPciDeviceClass {
	FuUdevDeviceClass parent_class;
};
