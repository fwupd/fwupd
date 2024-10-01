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

guint16
fu_pci_device_get_subsystem_vid(FuPciDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_pci_device_get_subsystem_pid(FuPciDevice *self) G_GNUC_NON_NULL(1);
void
fu_pci_device_set_subsystem_vid(FuPciDevice *self, guint16 subsystem_vid) G_GNUC_NON_NULL(1);
void
fu_pci_device_set_subsystem_pid(FuPciDevice *self, guint16 subsystem_pid) G_GNUC_NON_NULL(1);
guint8
fu_pci_device_get_revision(FuPciDevice *self) G_GNUC_NON_NULL(1);
void
fu_pci_device_set_revision(FuPciDevice *self, guint8 revision) G_GNUC_NON_NULL(1);
